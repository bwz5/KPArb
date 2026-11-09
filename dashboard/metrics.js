const fs = require("fs");
const path = require("path");
const { execFile } = require("child_process");

const ROOT = path.resolve(__dirname, "..");
const DECODER = path.join(ROOT, "build/release/src/tools/kparb_logdecode");
const LOG_DIRECTORY = path.join(ROOT, "data/logs");
const CONSOLE_LOG = path.join(ROOT, "data/overnight.log");
const MATCHED_PAIRS = path.join(ROOT, "data/matched_pairs.json");
const CONFIG = path.join(ROOT, "config/config.json");
const HOUR_MS = 3_600_000;
const WINDOW_HOURS = 48;
const RECENT_HOURS = 24;
const CONSOLE_TAIL_BYTES = 4 * 1024 * 1024;
const DECODE_BUFFER_BYTES = 1024 * 1024 * 1024;
const MINIMUM_REDECODE_MS = 20_000;
const PAPER_REFRESH_MS = 60_000;
const DEFAULT_EXECUTION = {
  mode: "disabled",
  kalshiBudgetDollars: 250,
  polymarketBudgetDollars: 250,
  rebalanceHours: 24,
  maximumDollarsPerTrade: 10,
  maximumLossDollars: 25,
  killSwitchPath: "data/KILL",
  journalPath: "data/executions.jsonl",
};

const decodeCache = new Map();
let paperCache = { at: 0, value: null, error: null, pending: null };

function run(command, args, options = {}) {
  return new Promise((resolve, reject) => {
    execFile(command, args, { cwd: ROOT, maxBuffer: DECODE_BUFFER_BYTES, ...options }, (error, stdout, stderr) => {
      if (error) {
        reject(new Error(stderr || error.message));
      } else {
        resolve(stdout);
      }
    });
  });
}

function parseCsv(text) {
  const lines = text.split("\n").filter(Boolean);
  if (lines.length === 0) {
    return [];
  }
  const header = lines[0].split(",");
  return lines.slice(1).map((line) => {
    const cells = line.split(",");
    const row = {};
    header.forEach((name, index) => {
      row[name] = cells[index];
    });
    return row;
  });
}

function readAnchors(file) {
  const handle = fs.openSync(file, "r");
  const buffer = Buffer.alloc(32);
  fs.readSync(handle, buffer, 0, 32, 0);
  fs.closeSync(handle);
  return { wall: buffer.readBigInt64LE(16), steady: buffer.readBigInt64LE(24) };
}

function wallMs(anchors, steadyText) {
  return Number((anchors.wall + BigInt(steadyText) - anchors.steady) / 1_000_000n);
}

async function decode(file, type) {
  const size = fs.statSync(file).size;
  const key = `${file}|${type}`;
  const cached = decodeCache.get(key);
  if (cached && (cached.size === size || Date.now() - cached.at < MINIMUM_REDECODE_MS)) {
    return cached.rows;
  }
  const rows = parseCsv(await run(DECODER, [file, "--type", type, "--csv"]));
  decodeCache.set(key, { size, at: Date.now(), rows });
  return rows;
}

function logFiles(nowMs) {
  if (!fs.existsSync(LOG_DIRECTORY)) {
    return [];
  }
  const files = fs.readdirSync(LOG_DIRECTORY).filter((name) => /^kparb-.*\.bin$/.test(name)).sort()
    .map((name) => path.join(LOG_DIRECTORY, name))
    .map((file) => ({ file, anchors: readAnchors(file), modifiedMs: fs.statSync(file).mtimeMs }));
  const windowStart = nowMs - WINDOW_HOURS * HOUR_MS;
  return files.filter((entry, index) => index === files.length - 1 || entry.modifiedMs >= windowStart);
}

function hourBuckets(nowMs) {
  const end = Math.ceil(nowMs / HOUR_MS) * HOUR_MS;
  const buckets = [];
  for (let start = end - WINDOW_HOURS * HOUR_MS; start < end; start += HOUR_MS) {
    buckets.push({ start, streamingMs: 0, opportunities: 0 });
  }
  return buckets;
}

function addInterval(buckets, from, to) {
  for (const bucket of buckets) {
    const overlap = Math.min(to, bucket.start + HOUR_MS) - Math.max(from, bucket.start);
    if (overlap > 0) {
      bucket.streamingMs += overlap;
    }
  }
}

function findProcess() {
  return run("pgrep", ["-x", "kparb"])
    .then(async (output) => {
      const pid = output.trim().split("\n")[0];
      const elapsed = (await run("ps", ["-o", "etime=", "-p", pid])).trim();
      return { running: true, pid: Number(pid), elapsed };
    })
    .catch(() => ({ running: false }));
}

async function feedMetrics(files, buckets, nowMs, isRunning) {
  const recentStart = nowMs - RECENT_HOURS * HOUR_MS;
  const counts = { disconnected: 0, sequence_gap: 0, resync_requested: 0, queue_overflow: 0, parse_error: 0, authentication_failed: 0 };
  let current = { Kalshi: false, Polymarket: false };
  let streamingRecentMs = 0;
  for (let index = 0; index < files.length; index += 1) {
    const { file, anchors } = files[index];
    const rows = await decode(file, "feed_event");
    const state = { Kalshi: false, Polymarket: false };
    let streamingSince = null;
    let lastTime = wallMs(anchors, anchors.steady.toString());
    for (const row of rows) {
      const time = wallMs(anchors, row.timestamp_ns);
      lastTime = time;
      if (row.event === "connected") {
        state[row.venue] = true;
      } else if (row.event === "disconnected") {
        state[row.venue] = false;
      }
      if (time >= recentStart && counts[row.event] !== undefined) {
        counts[row.event] += 1;
      }
      const streaming = state.Kalshi && state.Polymarket;
      if (streaming && streamingSince === null) {
        streamingSince = time;
      } else if (!streaming && streamingSince !== null) {
        addInterval(buckets, streamingSince, time);
        streamingRecentMs += Math.max(0, time - Math.max(streamingSince, recentStart));
        streamingSince = null;
      }
    }
    const isNewest = index === files.length - 1;
    const end = isNewest && isRunning ? nowMs : lastTime;
    if (streamingSince !== null) {
      addInterval(buckets, streamingSince, end);
      streamingRecentMs += Math.max(0, end - Math.max(streamingSince, recentStart));
    }
    if (isNewest) {
      current = isRunning ? state : { Kalshi: false, Polymarket: false };
    }
  }
  return { counts, current, streamingRecentFraction: streamingRecentMs / (RECENT_HOURS * HOUR_MS) };
}

async function opportunityMetrics(files, buckets) {
  const recent = [];
  for (const { file, anchors } of files) {
    const opened = await decode(file, "opportunity");
    const closed = await decode(file, "opportunity_closed");
    const events = opened.map((row) => ({ kind: "open", row })).concat(closed.map((row) => ({ kind: "close", row })))
      .sort((left, right) => (BigInt(left.row.timestamp_ns) < BigInt(right.row.timestamp_ns) ? -1 : 1));
    const open = new Map();
    for (const { kind, row } of events) {
      const key = `${row.kalshi_ticker}|${row.polymarket_identifier}|${row.direction}`;
      const time = wallMs(anchors, row.timestamp_ns);
      if (kind === "close") {
        const episode = open.get(key);
        if (episode) {
          episode.durationMs = Number(row.open_duration_ns) / 1e6;
          open.delete(key);
        }
        continue;
      }
      if (open.has(key)) {
        const episode = open.get(key);
        episode.bestExpectedProfit = Math.max(episode.bestExpectedProfit, Number(row.expected_profit));
        continue;
      }
      const episode = {
        time, kalshiTicker: row.kalshi_ticker, polymarket: row.polymarket_identifier, direction: row.direction,
        edge: Number(row.edge), contracts: Number(row.contracts), bestExpectedProfit: Number(row.expected_profit), durationMs: null,
      };
      open.set(key, episode);
      recent.push(episode);
      const bucket = buckets.find((candidate) => time >= candidate.start && time < candidate.start + HOUR_MS);
      if (bucket) {
        bucket.opportunities += 1;
      }
    }
  }
  return recent.slice(-25).reverse();
}

async function engineMetrics(files) {
  if (files.length === 0) {
    return null;
  }
  const newest = files[files.length - 1];
  const rows = await decode(newest.file, "engine_stats");
  const last = rows[rows.length - 1];
  if (!last) {
    return null;
  }
  return {
    time: wallMs(newest.anchors, last.timestamp_ns),
    updates: Number(last.updates),
    openOpportunities: Number(last.open),
    synchronizedLadders: Number(last.synchronized_ladders),
    kalshiQueue: Number(last.kalshi_queue),
    polymarketQueue: Number(last.polymarket_queue),
    droppedLogRecords: Number(last.dropped_log_records),
  };
}

function consoleMetrics() {
  if (!fs.existsSync(CONSOLE_LOG)) {
    return {};
  }
  const size = fs.statSync(CONSOLE_LOG).size;
  const start = Math.max(0, size - CONSOLE_TAIL_BYTES);
  const handle = fs.openSync(CONSOLE_LOG, "r");
  const buffer = Buffer.alloc(size - start);
  fs.readSync(handle, buffer, 0, buffer.length, start);
  fs.closeSync(handle);
  const lines = buffer.toString("utf8").split("\n");
  let restartIndex = 0;
  lines.forEach((line, index) => {
    if (line.startsWith("===== restart")) {
      restartIndex = index;
    }
  });
  const sinceRestart = lines.slice(restartIndex);
  const lastMatching = (pattern) => [...sinceRestart].reverse().find((line) => pattern.test(line)) || null;
  const streamingLine = lastMatching(/^streaming \d+ pairs \(/);
  const cycleLine = lastMatching(/^=== discovery cycle \d+/);
  const matchingLine = lastMatching(/^matching finished:/);
  const numberAfter = (line, label) => {
    const found = line && line.match(new RegExp(`${label}=(\\d+)`));
    return found ? Number(found[1]) : null;
  };
  return {
    restartLine: lines[restartIndex] && lines[restartIndex].startsWith("=====") ? lines[restartIndex].replace(/=/g, "").trim() : null,
    streamingPairs: streamingLine ? Number(streamingLine.match(/^streaming (\d+)/)[1]) : null,
    discoveryCycle: cycleLine ? Number(cycleLine.match(/cycle (\d+)/)[1]) : null,
    acceptedPairs: numberAfter(matchingLine, "accepted pairs"),
    entityRuleMatches: numberAfter(matchingLine, "entity rule matches"),
    settlementConflicts: numberAfter(matchingLine, "settlement conflicts"),
    excludedBySeries: numberAfter(matchingLine, "excluded by series"),
    discoveryFailures: sinceRestart.filter((line) => line.startsWith("discovery failed")).length,
    lastDiscoveryFailure: lastMatching(/^discovery failed/),
    halted: lastMatching(/HALTED: /),
  };
}

function pairCoverage() {
  if (!fs.existsSync(MATCHED_PAIRS)) {
    return { total: 0, bySeries: [] };
  }
  const pairs = JSON.parse(fs.readFileSync(MATCHED_PAIRS, "utf8")).pairs || [];
  const counts = new Map();
  for (const pair of pairs) {
    const series = pair.kalshiTicker.split("-")[0];
    counts.set(series, (counts.get(series) || 0) + 1);
  }
  return { total: pairs.length, bySeries: [...counts.entries()].sort((left, right) => right[1] - left[1]).slice(0, 12) };
}

function executionConfig() {
  let configured = {};
  try {
    configured = JSON.parse(fs.readFileSync(CONFIG, "utf8")).execution || {};
  } catch (error) {
    configured = {};
  }
  const merged = { ...DEFAULT_EXECUTION, ...configured };
  if (process.env.KPARB_JOURNAL) {
    merged.journalPath = process.env.KPARB_JOURNAL;
  }
  return merged;
}

function refreshPaper(journalPath) {
  if (paperCache.pending) {
    return paperCache.pending;
  }
  paperCache.pending = run("python3", ["scripts/paper_pnl.py", "--journal", journalPath, "--json"], { maxBuffer: 256 * 1024 * 1024 })
    .then((output) => {
      paperCache = { at: Date.now(), value: JSON.parse(output), error: null, pending: null };
    })
    .catch((error) => {
      paperCache = { ...paperCache, at: Date.now(), error: error.message.slice(0, 400), pending: null };
    });
  return paperCache.pending;
}

async function paperMetrics(execution, botStartMs, nowMs) {
  if (!paperCache.value || Date.now() - paperCache.at > PAPER_REFRESH_MS) {
    const refresh = refreshPaper(execution.journalPath);
    if (!paperCache.value) {
      await refresh;
    }
  }
  const trades = (paperCache.value && paperCache.value.trades) || [];
  const periodMs = execution.rebalanceHours * HOUR_MS;
  const periodStart = botStartMs && periodMs > 0 ? botStartMs + Math.floor((nowMs - botStartMs) / periodMs) * periodMs : nowMs - RECENT_HOURS * HOUR_MS;
  const inPeriod = trades.filter((trade) => trade.time_ns / 1e6 >= periodStart);
  const spent = (field) => inPeriod.reduce((total, trade) => total + (trade[field] || 0), 0);
  return {
    totals: (paperCache.value && paperCache.value.totals) || {},
    trades: trades.map((trade) => ({ ...trade, timeMs: trade.time_ns / 1e6 })),
    error: paperCache.error,
    period: {
      start: periodStart, end: periodStart + periodMs,
      kalshiSpent: spent("kalshi_dollars"), polymarketSpent: spent("polymarket_dollars"),
      kalshiBudget: execution.kalshiBudgetDollars, polymarketBudget: execution.polymarketBudgetDollars,
    },
  };
}

async function summary() {
  const nowMs = Date.now();
  const files = logFiles(nowMs);
  const processInfo = await findProcess();
  const buckets = hourBuckets(nowMs);
  const execution = executionConfig();
  const newest = files[files.length - 1];
  const botStartMs = newest ? Number(newest.anchors.wall / 1_000_000n) : null;
  const [feeds, opportunities, engine, paper] = await Promise.all([
    feedMetrics(files, buckets, nowMs, processInfo.running),
    opportunityMetrics(files, buckets),
    engineMetrics(files),
    paperMetrics(execution, botStartMs, nowMs),
  ]);
  const killSwitchPath = path.resolve(ROOT, execution.killSwitchPath);
  return {
    now: nowMs,
    bot: { ...processInfo, startedAt: botStartMs, logFile: newest ? path.relative(ROOT, newest.file) : null },
    feeds,
    engine,
    hours: buckets.map((bucket) => ({ start: bucket.start, streamingFraction: bucket.streamingMs / HOUR_MS, opportunities: bucket.opportunities })),
    opportunities,
    console: consoleMetrics(),
    coverage: pairCoverage(),
    execution: { mode: execution.mode, maximumDollarsPerTrade: execution.maximumDollarsPerTrade, maximumLossDollars: execution.maximumLossDollars,
      rebalanceHours: execution.rebalanceHours, killSwitchEngaged: fs.existsSync(killSwitchPath), killSwitchPath: execution.killSwitchPath },
    paper,
  };
}

function setKillSwitch(engaged) {
  const killSwitchPath = path.resolve(ROOT, executionConfig().killSwitchPath);
  if (engaged) {
    fs.mkdirSync(path.dirname(killSwitchPath), { recursive: true });
    fs.writeFileSync(killSwitchPath, `engaged from the dashboard at ${new Date().toISOString()}\n`);
  } else if (fs.existsSync(killSwitchPath)) {
    fs.unlinkSync(killSwitchPath);
  }
  return fs.existsSync(killSwitchPath);
}

module.exports = { summary, setKillSwitch };
