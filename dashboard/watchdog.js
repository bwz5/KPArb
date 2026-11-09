const fs = require("fs");
const path = require("path");
const { spawn, execFile } = require("child_process");
const { summary } = require("./metrics");

const ROOT = path.resolve(__dirname, "..");
const ENV_FILE = path.join(ROOT, ".env");
const STATE_FILE = process.env.KPARB_WATCHDOG_STATE || path.join(ROOT, "data/watchdog_state.json");
const BOT_BINARY = path.join(ROOT, "build/release/src/app/kparb");
const CONSOLE_LOG = path.join(ROOT, "data/overnight.log");
const HOLD_FILE = path.join(ROOT, "data/HOLD");
const CHECK_INTERVAL_MS = 60_000;
const FEEDS_DOWN_ALERT_MS = 5 * 60_000;
const MAXIMUM_RESTARTS_PER_HOUR = 3;
const HOUR_MS = 3_600_000;
const DISCOVERY_FAILURE_ALERT_COUNT = 3;
const DAILY_SUMMARY_HOUR = 9;
const ENTITY_REBUILD_HOUR = 4;
const WEEKLY_REPORT_DAY = 1;
const SCRIPT_TIMEOUT_MS = 30 * 60_000;
const HEARTBEAT_FILE = path.join(ROOT, "data/watchdog_heartbeat");
const FAILURE_REPEAT_MS = 15 * 60_000;
const HEARTBEAT_INTERVAL_MS = 30_000;
const MAXIMUM_FAILURE_GROUPS = 15;
const ENGINE_STALL_MS = 3 * 60_000;
const MINIMUM_FREE_DISK_BYTES = 5 * 1024 ** 3;
const FAILURE_PATTERN = new RegExp([
  "^fatal",
  "^discovery failed",
  "^could not",
  "^kalshi order api (did not|check failed)",
  "Kalshi rejected",
  "feed_event \\S+ (connection_error|authentication_failed|sequence_gap|queue_overflow|parse_error|server_error|resync_requested)",
  "language model was disabled",
  "HALTED",
  "too many distinct",
  "Unable to",
  "terminate called|Segmentation fault|Abort trap|libc\\+\\+abi",
].join("|"), "i");
const IGNORED_PATTERN = /catalog fetch cancelled by shutdown/;

function readEnvironmentFile() {
  const values = {};
  if (!fs.existsSync(ENV_FILE)) {
    return values;
  }
  for (const line of fs.readFileSync(ENV_FILE, "utf8").split("\n")) {
    const match = line.match(/^\s*([A-Z0-9_]+)\s*=\s*(.*)\s*$/);
    if (match) {
      values[match[1]] = match[2].replace(/^"(.*)"$/, "$1");
    }
  }
  return values;
}

function webhookUrl() {
  return process.env.KPARB_SLACK_WEBHOOK_URL || readEnvironmentFile().KPARB_SLACK_WEBHOOK_URL || "";
}

function loadState() {
  try {
    return JSON.parse(fs.readFileSync(STATE_FILE, "utf8"));
  } catch (error) {
    return {};
  }
}

function saveState(state) {
  fs.mkdirSync(path.dirname(STATE_FILE), { recursive: true });
  fs.writeFileSync(STATE_FILE, JSON.stringify(state, null, 1));
}

function log(message) {
  console.log(`${new Date().toISOString()} ${message}`);
}

async function post(text) {
  log(`notify: ${text.split("\n")[0]}`);
  const url = webhookUrl();
  if (!url) {
    return;
  }
  try {
    const response = await fetch(url, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ text }) });
    if (!response.ok) {
      log(`slack webhook returned ${response.status}`);
    }
  } catch (error) {
    log(`slack webhook failed: ${error.message}`);
  }
}

function runScript(args) {
  return new Promise((resolve) => {
    execFile("python3", args, { cwd: ROOT, timeout: SCRIPT_TIMEOUT_MS, maxBuffer: 64 * 1024 * 1024 }, (error, stdout, stderr) => {
      resolve({ ok: !error, output: (stdout || "").trim(), error: error ? (stderr || error.message).trim().slice(-800) : "" });
    });
  });
}

function money(value) {
  const sign = value < 0 ? "-" : "+";
  return `${sign}$${Math.abs(value).toFixed(2)}`;
}

function normalizeFailure(line) {
  return line.replace(/[0-9]+/g, "N").replace(/after_cursor=[^&\s]+/g, "after_cursor=…").slice(0, 160);
}

async function reportFailures(state, lines, now) {
  const groups = new Map();
  for (const line of lines) {
    const key = normalizeFailure(line);
    const group = groups.get(key) || { example: line, count: 0 };
    group.count += 1;
    groups.set(key, group);
  }
  const lastPosted = state.failureLastPosted || (state.failureLastPosted = {});
  const suppressed = state.failureSuppressed || (state.failureSuppressed = {});
  const due = [];
  for (const [key, group] of groups) {
    if (lastPosted[key] && now - lastPosted[key] < FAILURE_REPEAT_MS) {
      suppressed[key] = (suppressed[key] || 0) + group.count;
      continue;
    }
    const total = group.count + (suppressed[key] || 0);
    delete suppressed[key];
    lastPosted[key] = now;
    due.push(`• ${total > 1 ? `${total}× ` : ""}\`${group.example.slice(0, 300)}\``);
  }
  for (const key of Object.keys(lastPosted)) {
    if (now - lastPosted[key] > 24 * HOUR_MS) {
      delete lastPosted[key];
      delete suppressed[key];
    }
  }
  if (due.length) {
    const extra = due.length > MAXIMUM_FAILURE_GROUPS ? `\n…and ${due.length - MAXIMUM_FAILURE_GROUPS} more kinds` : "";
    await post(`:x: *KPArb failure${due.length > 1 ? "s" : ""}*\n${due.slice(0, MAXIMUM_FAILURE_GROUPS).join("\n")}${extra}`);
  }
}

function newConsoleLines(state) {
  if (!fs.existsSync(CONSOLE_LOG)) {
    return [];
  }
  const size = fs.statSync(CONSOLE_LOG).size;
  if (state.consoleOffset === undefined || state.consoleOffset > size) {
    state.consoleOffset = size;
    return [];
  }
  const length = Math.min(size - state.consoleOffset, 8 * 1024 * 1024);
  if (length <= 0) {
    return [];
  }
  const buffer = Buffer.alloc(length);
  const handle = fs.openSync(CONSOLE_LOG, "r");
  fs.readSync(handle, buffer, 0, length, state.consoleOffset);
  fs.closeSync(handle);
  const text = buffer.toString("utf8");
  const complete = text.lastIndexOf("\n") + 1;
  state.consoleOffset += Buffer.byteLength(text.slice(0, complete));
  return text.slice(0, complete).split("\n").filter((line) => line && !line.startsWith("  [") && FAILURE_PATTERN.test(line) && !IGNORED_PATTERN.test(line));
}

function otherFailures(state, data, now) {
  const failures = [];
  for (const trade of data.paper.trades || []) {
    if (trade.timeMs > (state.lastTradeErrorMs || 0) && (trade.first_error || trade.second_error)) {
      failures.push(`order error on ${trade.kalshi_ticker}: ${trade.first_error || trade.second_error}`);
    }
  }
  if ((data.paper.trades || []).length) {
    state.lastTradeErrorMs = Math.max(state.lastTradeErrorMs || 0, ...data.paper.trades.map((trade) => trade.timeMs));
  }
  if (data.paper.error) {
    failures.push(`paper P&L settlement failed: ${data.paper.error}`);
  }
  if (data.bot.running && data.engine && now - data.engine.time > ENGINE_STALL_MS && data.console.streamingPairs) {
    failures.push(`engine stats have not updated for ${Math.round((now - data.engine.time) / 60_000)} minutes while the bot is running`);
  }
  try {
    const disk = fs.statfsSync(ROOT);
    const free = disk.bavail * disk.bsize;
    if (free < MINIMUM_FREE_DISK_BYTES) {
      failures.push(`low disk space: ${(free / 1024 ** 3).toFixed(1)} GB free`);
    }
  } catch (error) {
    failures.push(`could not check disk space: ${error.message}`);
  }
  return failures;
}

function restartBot(state, now) {
  state.restarts = (state.restarts || []).filter((time) => now - time < HOUR_MS);
  if (state.restarts.length >= MAXIMUM_RESTARTS_PER_HOUR) {
    return false;
  }
  state.restarts.push(now);
  fs.appendFileSync(CONSOLE_LOG, `===== restart ${new Date(now).toString()} by the watchdog =====\n`);
  const output = fs.openSync(CONSOLE_LOG, "a");
  const child = spawn("caffeinate", ["-is", BOT_BINARY], { cwd: ROOT, detached: true, stdio: ["ignore", output, output] });
  child.unref();
  return true;
}

async function checkHealth(state, data, now) {
  const alerts = state.alerts || (state.alerts = {});
  const raise = async (key, text) => {
    if (!alerts[key]) {
      alerts[key] = now;
      await post(text);
    }
  };
  const clear = async (key, text) => {
    if (alerts[key]) {
      delete alerts[key];
      if (text) {
        await post(text);
      }
    }
  };

  if (!data.bot.running && fs.existsSync(HOLD_FILE)) {
    await raise("botHeld", ":pause_button: KPArb is stopped and data/HOLD exists, so the watchdog is not restarting it.");
    return;
  }
  await clear("botHeld");
  if (!data.bot.running) {
    const restarted = restartBot(state, now);
    await raise("botStopped", restarted
      ? ":rotating_light: KPArb stopped. The watchdog restarted it."
      : `:rotating_light: KPArb stopped and has already been restarted ${MAXIMUM_RESTARTS_PER_HOUR} times this hour. It needs a look.`);
    if (restarted) {
      delete alerts.botStopped;
    }
    return;
  }
  await clear("botStopped", ":white_check_mark: KPArb is running again.");

  const streaming = data.feeds.current.Kalshi && data.feeds.current.Polymarket;
  if (streaming) {
    state.feedsDownSince = null;
    await clear("feedsDown", ":white_check_mark: Both feeds are connected again.");
  } else {
    state.feedsDownSince = state.feedsDownSince || now;
    if (now - state.feedsDownSince >= FEEDS_DOWN_ALERT_MS) {
      const down = ["Kalshi", "Polymarket"].filter((venue) => !data.feeds.current[venue]).join(" and ");
      await raise("feedsDown", `:warning: ${down} feed down for ${Math.round((now - state.feedsDownSince) / 60_000)} minutes.`);
    }
  }

  const trades = data.paper.trades || [];
  const newTrades = trades.filter((trade) => trade.timeMs > (state.lastTradeMs || 0));
  for (const trade of newTrades) {
    if (trade.unhedged > 0) {
      await post(`:rotating_light: Unhedged position: ${trade.unhedged} contracts on ${trade.kalshi_ticker}. Trading is halted.`);
    } else if (trade.unwound > 0) {
      await post(`:warning: Unwound ${trade.unwound} contracts on ${trade.kalshi_ticker} after the second leg missed.`);
    }
  }
  if (trades.length) {
    state.lastTradeMs = Math.max(...trades.map((trade) => trade.timeMs));
  }
  const halted = [...trades].reverse().find((trade) => trade.halted);
  if (halted) {
    await raise(`halted:${halted.halted}`, `:octagonal_sign: Paper trading halted: ${halted.halted}`);
  }

  if (data.execution.killSwitchEngaged) {
    await raise("killSwitch", ":octagonal_sign: Kill switch is engaged. No new trades.");
  } else {
    await clear("killSwitch", ":white_check_mark: Kill switch released.");
  }

  const failures = data.console.discoveryFailures || 0;
  if (failures >= DISCOVERY_FAILURE_ALERT_COUNT) {
    await raise(`discoveryFailures:${data.bot.pid}`, `:warning: ${failures} catalog fetches have failed since the last restart. Latest: ${data.console.lastDiscoveryFailure || "unknown"}`);
  }
  if (data.engine && data.engine.droppedLogRecords > 0) {
    await raise(`droppedRecords:${data.bot.pid}`, `:warning: The engine dropped ${data.engine.droppedLogRecords} log records this session.`);
  }
}

function localDayKey(ms) {
  const date = new Date(ms);
  return `${date.getFullYear()}-${date.getMonth() + 1}-${date.getDate()}`;
}

async function dailySummary(data) {
  const totals = data.paper.totals || {};
  const dayAgo = data.now - 24 * HOUR_MS;
  const recent = (data.paper.trades || []).filter((trade) => trade.timeMs >= dayAgo);
  const recentPnl = recent.reduce((total, trade) => total + (trade.realized !== null ? trade.realized : trade.expected), 0);
  const opportunities = data.hours.filter((hour) => hour.start >= dayAgo).reduce((total, hour) => total + hour.opportunities, 0);
  await post([
    ":bar_chart: *KPArb daily summary*",
    `Paper P&L, last 24 h: ${money(recentPnl)} on ${recent.length} trades`,
    `Paper P&L, all time: ${money((totals.realized || 0) + (totals.pending_expected || 0))} (${money(totals.realized || 0)} settled, ${totals.pending || 0} trades pending)`,
    `Streaming uptime, last 24 h: ${(data.feeds.streamingRecentFraction * 100).toFixed(1)}%`,
    `Pairs streaming: ${data.console.streamingPairs ?? "unknown"}; opportunities opened, last 24 h: ${opportunities}`,
    `Spend this period: Kalshi $${data.paper.period.kalshiSpent.toFixed(2)} of $${data.paper.period.kalshiBudget}, Polymarket $${data.paper.period.polymarketSpent.toFixed(2)} of $${data.paper.period.polymarketBudget}`,
  ].join("\n"));
}

async function rebuildEntities() {
  const before = await runScript(["-c", "import json;d=json.load(open('config/entities.json'));print(len(d['leagues']),sum(len(l['entities']) for l in d['leagues']))"]);
  const result = await runScript(["scripts/build_entities.py"]);
  if (!result.ok) {
    await post(`:warning: Nightly entity rebuild failed: ${result.error}`);
    return;
  }
  await post(`:books: Nightly entity rebuild: ${result.output.split("\n").pop()} (was ${before.output.replace(" ", " leagues, ")} entities). New entities apply from the next discovery cycle.`);
}

async function weeklyReport() {
  const result = await runScript(["scripts/weekly_report.py", "--days", "7"]);
  await post(result.ok ? result.output : `:warning: Weekly report failed: ${result.error}`);
}

async function runSchedules(state, data, now) {
  const date = new Date(now);
  const today = localDayKey(now);
  const canPost = Boolean(webhookUrl());
  if (canPost && date.getHours() >= DAILY_SUMMARY_HOUR && state.lastDailySummary !== today) {
    state.lastDailySummary = today;
    await dailySummary(data);
  }
  if (date.getHours() >= ENTITY_REBUILD_HOUR && date.getHours() < DAILY_SUMMARY_HOUR && state.lastEntityRebuild !== today) {
    state.lastEntityRebuild = today;
    saveState(state);
    await rebuildEntities();
  }
  if (canPost && date.getDay() === WEEKLY_REPORT_DAY && date.getHours() >= DAILY_SUMMARY_HOUR && state.lastWeeklyReport !== today) {
    state.lastWeeklyReport = today;
    saveState(state);
    await weeklyReport();
  }
}

let checking = false;

async function tick() {
  if (checking) {
    return;
  }
  checking = true;
  const state = loadState();
  const now = Date.now();
  try {
    const consoleFailures = newConsoleLines(state);
    const data = await summary();
    await checkHealth(state, data, now);
    await reportFailures(state, consoleFailures.concat(otherFailures(state, data, now)), now);
    await runSchedules(state, data, now);
  } catch (error) {
    log(`check failed: ${error.message}`);
    await reportFailures(state, [`watchdog check failed: ${error.message}`], now);
  } finally {
    saveState(state);
    checking = false;
  }
}

if (require.main === module) {
  const once = process.argv.includes("--once");
  const test = process.argv.includes("--test");
  if (test) {
    post(":wave: KPArb watchdog test message. Alerts will arrive in this channel.").then(() => process.exit(0));
  } else {
    log(`watchdog started; Slack ${webhookUrl() ? "configured" : "not configured (set KPARB_SLACK_WEBHOOK_URL in .env)"}`);
    process.on("uncaughtException", (error) => {
      post(`:x: *KPArb watchdog crashed*: ${error.message}. The guard restarts it within 5 minutes.`).finally(() => process.exit(1));
    });
    process.on("unhandledRejection", (error) => {
      post(`:x: *KPArb watchdog error*: ${error && error.message ? error.message : error}`);
    });
    const beat = () => fs.writeFileSync(HEARTBEAT_FILE, String(Date.now()));
    beat();
    if (!once) {
      setInterval(beat, HEARTBEAT_INTERVAL_MS);
    }
    tick().then(() => {
      if (!once) {
        setInterval(tick, CHECK_INTERVAL_MS);
      }
    });
  }
}

module.exports = { tick };
