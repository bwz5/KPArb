const REFRESH_MS = 15_000;
const HOUR_MS = 3_600_000;
const SVG_NS = "http://www.w3.org/2000/svg";
const PLOT = { top: 12, right: 64, bottom: 28, left: 52 };
const CHART_HEIGHT = 240;
const MAXIMUM_BAR_WIDTH = 24;
const BAR_GAP = 2;
const BAR_RADIUS = 4;
const LADDERS_PER_PAIR = 4;

let latest = null;

const tooltip = document.getElementById("tooltip");

function element(tag, attributes = {}, children = []) {
  const node = document.createElement(tag);
  for (const [name, value] of Object.entries(attributes)) {
    if (name === "text") {
      node.textContent = value;
    } else if (name === "className") {
      node.className = value;
    } else {
      node.setAttribute(name, value);
    }
  }
  for (const child of children) {
    node.append(child);
  }
  return node;
}

function svg(tag, attributes = {}) {
  const node = document.createElementNS(SVG_NS, tag);
  for (const [name, value] of Object.entries(attributes)) {
    node.setAttribute(name, value);
  }
  return node;
}

function money(value, { signed = false } = {}) {
  if (value === null || value === undefined || Number.isNaN(value)) {
    return "–";
  }
  const sign = value < 0 ? "−" : signed && value > 0 ? "+" : "";
  const absolute = Math.abs(value);
  const text = absolute >= 10_000 ? `${(absolute / 1000).toFixed(1)}K` : absolute.toFixed(2);
  return `${sign}$${text}`;
}

function percent(value) {
  return value === null || value === undefined ? "–" : `${(value * 100).toFixed(value >= 0.995 || value === 0 ? 0 : 1)}%`;
}

function count(value) {
  return value === null || value === undefined ? "–" : Number(value).toLocaleString();
}

function timeLabel(ms, withDay = true) {
  const date = new Date(ms);
  return date.toLocaleString(undefined, withDay ? { weekday: "short", hour: "numeric", minute: "2-digit" } : { hour: "numeric", minute: "2-digit" });
}

function dayKey(ms) {
  const date = new Date(ms);
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, "0")}-${String(date.getDate()).padStart(2, "0")}`;
}

function duration(ms) {
  if (ms === null || ms === undefined) {
    return "open";
  }
  if (ms < 1000) {
    return `${Math.round(ms)} ms`;
  }
  if (ms < 60_000) {
    return `${(ms / 1000).toFixed(1)} s`;
  }
  return `${(ms / 60_000).toFixed(1)} min`;
}

function status(kind, text) {
  const icons = { good: "✓", warning: "!", serious: "!", critical: "✕", neutral: "–" };
  return element("span", { className: `status status-${kind}` }, [
    element("span", { className: "status-icon", "aria-hidden": "true", text: icons[kind] || "–" }),
    element("span", { text }),
  ]);
}

function tile({ label, value, detail, statusKind, meter }) {
  const valueNode = element("div", { className: "value" });
  if (statusKind) {
    valueNode.append(status(statusKind, value));
  } else {
    valueNode.textContent = value;
  }
  const children = [element("div", { className: "label", text: label }), valueNode];
  if (meter) {
    const share = Math.max(0, Math.min(1, meter.share));
    const fillClass = share >= 1 ? "critical" : share >= 0.8 ? "warning" : "";
    const track = element("div", { className: "meter", role: "meter", "aria-valuemin": "0", "aria-valuemax": "100", "aria-valuenow": String(Math.round(share * 100)), "aria-label": label });
    const fill = element("div", { className: `meter-fill ${fillClass}` });
    fill.style.width = `${share * 100}%`;
    track.append(fill);
    children.push(track);
  }
  if (detail) {
    children.push(element("div", { className: "detail", text: detail }));
  }
  return element("div", { className: "card tile" }, children);
}

function showTooltip(event, title, rows) {
  tooltip.replaceChildren(element("div", { className: "tooltip-title", text: title }));
  for (const row of rows) {
    const line = element("div", { className: "tooltip-row" });
    if (row.color) {
      const key = element("span", { className: "legend-key" });
      key.style.background = row.color;
      line.append(key);
    }
    line.append(element("span", { text: row.text }));
    tooltip.append(line);
  }
  tooltip.hidden = false;
  const margin = 12;
  const width = tooltip.offsetWidth;
  const height = tooltip.offsetHeight;
  const left = Math.min(window.innerWidth - width - margin, event.clientX + margin);
  const top = Math.max(margin, Math.min(window.innerHeight - height - margin, event.clientY - height - margin));
  tooltip.style.left = `${Math.max(margin, left)}px`;
  tooltip.style.top = `${top}px`;
}

function hideTooltip() {
  tooltip.hidden = true;
}

function niceTicks(minimum, maximum, target = 4) {
  if (minimum === maximum) {
    maximum = minimum + 1;
  }
  const rough = (maximum - minimum) / target;
  const magnitude = 10 ** Math.floor(Math.log10(rough));
  const step = [1, 2, 2.5, 5, 10].map((factor) => factor * magnitude).find((candidate) => candidate >= rough);
  const start = Math.floor(minimum / step) * step;
  const end = Math.ceil(maximum / step) * step;
  const ticks = [];
  for (let value = start; value <= end + step / 2; value += step) {
    ticks.push(Number(value.toFixed(10)));
  }
  return ticks;
}

function cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function frame(container, yTicks, yFormat) {
  const width = Math.max(320, container.clientWidth);
  const root = svg("svg", { viewBox: `0 0 ${width} ${CHART_HEIGHT}`, height: CHART_HEIGHT, role: "img" });
  const plotWidth = width - PLOT.left - PLOT.right;
  const plotHeight = CHART_HEIGHT - PLOT.top - PLOT.bottom;
  const yMinimum = yTicks[0];
  const yMaximum = yTicks[yTicks.length - 1];
  const y = (value) => PLOT.top + plotHeight - ((value - yMinimum) / (yMaximum - yMinimum)) * plotHeight;
  for (const tick of yTicks) {
    root.append(svg("line", { x1: PLOT.left, x2: PLOT.left + plotWidth, y1: y(tick), y2: y(tick), stroke: tick === 0 ? cssVar("--baseline") : cssVar("--grid"), "stroke-width": 1 }));
    const label = svg("text", { x: PLOT.left - 8, y: y(tick) + 4, "text-anchor": "end", class: "axis-label" });
    label.textContent = yFormat(tick);
    root.append(label);
  }
  return { root, width, plotWidth, plotHeight, y };
}

function timeAxis(chart, xMinimum, xMaximum, x) {
  const spanHours = (xMaximum - xMinimum) / HOUR_MS;
  const stepHours = spanHours <= 12 ? 2 : spanHours <= 36 ? 6 : spanHours <= 96 ? 12 : 24;
  const first = Math.ceil(xMinimum / (stepHours * HOUR_MS)) * stepHours * HOUR_MS;
  let lastRight = -Infinity;
  for (let tick = first; tick <= xMaximum; tick += stepHours * HOUR_MS) {
    const position = x(tick);
    const text = new Date(tick).toLocaleString(undefined, new Date(tick).getHours() === 0 ? { weekday: "short" } : { hour: "numeric" });
    const approximateWidth = text.length * 6.5;
    if (position - approximateWidth / 2 < lastRight + 8 || position + approximateWidth / 2 > chart.width - 4) {
      continue;
    }
    const label = svg("text", { x: position, y: CHART_HEIGHT - 8, "text-anchor": "middle", class: "axis-label" });
    label.textContent = text;
    chart.root.append(label);
    lastRight = position + approximateWidth / 2;
  }
}

function lineChart(container, series, { yFormat, title }) {
  container.replaceChildren();
  const points = series.flatMap((entry) => entry.points);
  if (points.length === 0) {
    container.append(element("div", { className: "empty", text: "No paper trades yet. The chart fills in as the executor trades." }));
    return;
  }
  const xMinimum = Math.min(...points.map((point) => point.x));
  const xMaximum = Math.max(latest ? latest.now : 0, ...points.map((point) => point.x));
  const values = points.map((point) => point.y).concat([0]);
  const chart = frame(container, niceTicks(Math.min(...values), Math.max(...values)), yFormat);
  const x = (value) => PLOT.left + (xMaximum === xMinimum ? chart.plotWidth / 2 : ((value - xMinimum) / (xMaximum - xMinimum)) * chart.plotWidth);
  chart.root.setAttribute("aria-label", title);
  timeAxis(chart, xMinimum, xMaximum, x);
  for (const entry of series) {
    if (entry.points.length === 0) {
      continue;
    }
    const stepped = [];
    entry.points.forEach((point, index) => {
      if (index > 0) {
        stepped.push([x(point.x), chart.y(entry.points[index - 1].y)]);
      }
      stepped.push([x(point.x), chart.y(point.y)]);
    });
    const last = entry.points[entry.points.length - 1];
    stepped.push([x(xMaximum), chart.y(last.y)]);
    chart.root.append(svg("path", {
      d: stepped.map(([px, py], index) => `${index === 0 ? "M" : "L"}${px.toFixed(1)},${py.toFixed(1)}`).join(" "),
      fill: "none", stroke: entry.color, "stroke-width": 2, "stroke-linejoin": "round", "stroke-linecap": "round",
    }));
    chart.root.append(svg("circle", { cx: x(xMaximum), cy: chart.y(last.y), r: 4, fill: entry.color, stroke: cssVar("--surface-1"), "stroke-width": 2 }));
    entry.endY = chart.y(last.y);
    entry.endValue = last.y;
  }
  const labelled = series.filter((entry) => entry.endY !== undefined).sort((left, right) => left.endY - right.endY);
  let previous = -Infinity;
  for (const entry of labelled) {
    if (entry.endY - previous < 14) {
      continue;
    }
    const label = svg("text", { x: x(xMaximum) + 8, y: entry.endY + 4, class: "end-label" });
    label.textContent = yFormat(entry.endValue);
    chart.root.append(label);
    previous = entry.endY;
  }
  const crosshair = svg("line", { y1: PLOT.top, y2: PLOT.top + chart.plotHeight, stroke: cssVar("--baseline"), "stroke-width": 1, visibility: "hidden" });
  chart.root.append(crosshair);
  const overlay = svg("rect", { x: PLOT.left, y: PLOT.top, width: chart.plotWidth, height: chart.plotHeight, fill: "transparent" });
  overlay.addEventListener("mousemove", (event) => {
    const bounds = chart.root.getBoundingClientRect();
    const scale = chart.width / bounds.width;
    const pointerX = (event.clientX - bounds.left) * scale;
    const time = xMinimum + ((pointerX - PLOT.left) / chart.plotWidth) * (xMaximum - xMinimum);
    crosshair.setAttribute("x1", pointerX);
    crosshair.setAttribute("x2", pointerX);
    crosshair.setAttribute("visibility", "visible");
    const rows = series.map((entry) => {
      const before = entry.points.filter((point) => point.x <= time);
      const value = before.length ? before[before.length - 1].y : 0;
      return { color: entry.color, text: `${entry.name}: ${yFormat(value)}` };
    });
    showTooltip(event, timeLabel(time), rows);
  });
  overlay.addEventListener("mouseleave", () => {
    crosshair.setAttribute("visibility", "hidden");
    hideTooltip();
  });
  chart.root.append(overlay);
  container.append(chart.root);
}

function columnChart(container, bars, { yFormat, title, color, emptyText, fixedMaximum, timeAxisLabels }) {
  container.replaceChildren();
  if (bars.length === 0) {
    container.append(element("div", { className: "empty", text: emptyText }));
    return;
  }
  const values = bars.map((bar) => bar.value).concat([0]);
  const ticks = niceTicks(Math.min(...values), fixedMaximum !== undefined ? Math.max(fixedMaximum, ...values) : Math.max(...values));
  const chart = frame(container, ticks, yFormat);
  chart.root.setAttribute("aria-label", title);
  const slot = chart.plotWidth / bars.length;
  const barWidth = Math.max(1, Math.min(MAXIMUM_BAR_WIDTH, slot - BAR_GAP));
  const zero = chart.y(0);
  bars.forEach((bar, index) => {
    const center = PLOT.left + slot * index + slot / 2;
    const top = chart.y(Math.max(0, bar.value));
    const bottom = chart.y(Math.min(0, bar.value));
    const height = Math.max(bar.value === 0 ? 0 : 1, bottom - top);
    const radius = Math.min(BAR_RADIUS, barWidth / 2, height);
    const left = center - barWidth / 2;
    const right = center + barWidth / 2;
    const path = bar.value >= 0
      ? `M${left},${zero} L${left},${top + radius} Q${left},${top} ${left + radius},${top} L${right - radius},${top} Q${right},${top} ${right},${top + radius} L${right},${zero} Z`
      : `M${left},${zero} L${left},${bottom - radius} Q${left},${bottom} ${left + radius},${bottom} L${right - radius},${bottom} Q${right},${bottom} ${right},${bottom - radius} L${right},${zero} Z`;
    if (height > 0) {
      chart.root.append(svg("path", { d: path, fill: color }));
    }
    const target = svg("rect", { x: PLOT.left + slot * index, y: PLOT.top, width: slot, height: chart.plotHeight, fill: "transparent" });
    target.addEventListener("mousemove", (event) => showTooltip(event, bar.title, [{ color, text: bar.detail || yFormat(bar.value) }]));
    target.addEventListener("mouseleave", hideTooltip);
    chart.root.append(target);
    if (!timeAxisLabels && (bars.length <= 10 || index % Math.ceil(bars.length / 8) === 0)) {
      const label = svg("text", { x: center, y: CHART_HEIGHT - 8, "text-anchor": "middle", class: "axis-label" });
      label.textContent = bar.label;
      chart.root.append(label);
    }
  });
  if (timeAxisLabels) {
    const start = bars[0].start;
    const end = bars[bars.length - 1].start + HOUR_MS;
    timeAxis(chart, start, end, (value) => PLOT.left + ((value - start) / (end - start)) * chart.plotWidth);
  }
  container.append(chart.root);
}

function table(node, columns, rows, emptyText) {
  const head = element("tr", {}, columns.map((column) => element("th", { className: column.number ? "number" : "", text: column.label })));
  const body = rows.length === 0
    ? [element("tr", {}, [element("td", { colspan: String(columns.length), text: emptyText })])]
    : rows.map((row) => element("tr", {}, columns.map((column) => {
      const cell = element("td", { className: column.number ? "number" : "" });
      const value = column.value(row);
      if (value instanceof Node) {
        cell.append(value);
      } else {
        cell.textContent = value;
      }
      if (column.tone) {
        const tone = column.tone(row);
        if (tone) {
          cell.classList.add(tone);
        }
      }
      return cell;
    })));
  node.replaceChildren(element("thead", {}, [head]), element("tbody", {}, body));
}

function tradeOutcome(trade) {
  if (trade.unhedged > 0) {
    return status("critical", "Unhedged");
  }
  if (trade.realized === null) {
    return status("neutral", trade.unwound > 0 ? "Pending, unwound" : "Pending");
  }
  if (trade.payout_per_pair !== null && Math.abs(trade.payout_per_pair - 1) > 1e-6) {
    return status("serious", `Paid ${trade.payout_per_pair.toFixed(2)}`);
  }
  return status("good", trade.unwound > 0 ? "Settled, unwound" : "Settled");
}

function renderPnl(data) {
  const trades = [...data.paper.trades].sort((left, right) => left.timeMs - right.timeMs);
  const totals = data.paper.totals;
  const realized = totals.realized || 0;
  const pendingExpected = totals.pending_expected || 0;
  const total = realized + pendingExpected;
  const hero = document.getElementById("hero-pnl");
  hero.textContent = trades.length ? money(total, { signed: true }) : "$0.00";
  document.getElementById("hero-detail").textContent = trades.length
    ? `${money(realized, { signed: true })} settled and ${money(pendingExpected, { signed: true })} expected on ${count(totals.pending || 0)} pending trades`
    : data.execution.mode === "paper" ? "Paper trading is on; no trades yet" : `Execution mode is ${data.execution.mode}`;
  const spent = totals.spent || 0;
  const hedged = trades.filter((trade) => trade.unhedged === 0 && trade.unwound === 0).length;
  const today = dayKey(data.now);
  const todayTrades = trades.filter((trade) => dayKey(trade.timeMs) === today);
  document.getElementById("pnl-tiles").replaceChildren(
    tile({ label: "Settled P&L", value: money(realized, { signed: true }), detail: `${count(totals.settled || 0)} trades settled` }),
    tile({ label: "Pending at hedged value", value: money(pendingExpected, { signed: true }), detail: `${count(totals.pending || 0)} trades waiting on results` }),
    tile({ label: "Return on capital traded", value: spent > 0 ? percent(total / spent) : "–", detail: `${money(spent)} traded` }),
    tile({ label: "Trades today", value: count(todayTrades.length), detail: `${count(trades.length)} all time` }),
    tile({ label: "Cleanly hedged", value: trades.length ? percent(hedged / trades.length) : "–", detail: `${count(trades.length - hedged)} needed a chase, unwind or were left open` }),
    tile({ label: "Settlement mismatches", value: count(totals.mismatched || 0), statusKind: (totals.mismatched || 0) > 0 ? "serious" : "good", detail: "Hedges that did not pay $1 a pair" }),
  );

  let settledRunning = 0;
  let totalRunning = 0;
  const settledPoints = [];
  const totalPoints = [];
  for (const trade of trades) {
    totalRunning += trade.realized !== null ? trade.realized : trade.expected;
    totalPoints.push({ x: trade.timeMs, y: totalRunning });
    if (trade.realized !== null) {
      settledRunning += trade.realized;
      settledPoints.push({ x: trade.timeMs, y: settledRunning });
    }
  }
  const series = [
    { name: "Including pending", color: cssVar("--series-1"), points: totalPoints },
    { name: "Settled only", color: cssVar("--series-2"), points: settledPoints },
  ];
  document.getElementById("pnl-legend").replaceChildren(...series.map((entry) => {
    const key = element("span", { className: "legend-key" });
    key.style.background = entry.color;
    return element("span", {}, [key, element("span", { text: entry.name })]);
  }));
  lineChart(document.getElementById("pnl-chart"), series, { yFormat: (value) => money(value, { signed: value !== 0 }), title: "Cumulative paper P&L" });

  const byDay = new Map();
  for (const trade of trades) {
    const key = dayKey(trade.timeMs);
    const entry = byDay.get(key) || { value: 0, trades: 0, ms: trade.timeMs };
    entry.value += trade.realized !== null ? trade.realized : trade.expected;
    entry.trades += 1;
    byDay.set(key, entry);
  }
  const dailyBars = [...byDay.entries()].map(([key, entry]) => ({
    label: new Date(entry.ms).toLocaleDateString(undefined, { month: "short", day: "numeric" }),
    title: new Date(entry.ms).toLocaleDateString(undefined, { weekday: "long", month: "short", day: "numeric" }),
    value: entry.value,
    detail: `${money(entry.value, { signed: true })} over ${entry.trades} trades`,
  }));
  columnChart(document.getElementById("daily-chart"), dailyBars, {
    yFormat: (value) => money(value, { signed: value !== 0 }), title: "P&L by day", color: cssVar("--series-1"), emptyText: "No paper trades yet.",
  });

  table(document.getElementById("trades-table"), [
    { label: "Time", value: (trade) => timeLabel(trade.timeMs) },
    { label: "Kalshi market", value: (trade) => trade.kalshi_ticker },
    { label: "Legs", value: (trade) => trade.direction.replace("+", " + ").replaceAll("_", " ") },
    { label: "Contracts", number: true, value: (trade) => count(trade.contracts) },
    { label: "Cost", number: true, value: (trade) => money(trade.cost) },
    { label: "P&L", number: true, value: (trade) => (trade.realized !== null ? money(trade.realized, { signed: true }) : `${money(trade.expected, { signed: true })} exp.`),
      tone: (trade) => ((trade.realized ?? trade.expected) > 0 ? "positive" : (trade.realized ?? trade.expected) < 0 ? "negative" : "") },
    { label: "Outcome", value: tradeOutcome },
  ], [...trades].reverse().slice(0, 50), "No paper trades yet.");
}

function renderHealth(data) {
  const { bot, feeds, engine, console: consoleInfo } = data;
  const pill = document.getElementById("bot-status");
  pill.className = `status-pill status-${bot.running ? "good" : "critical"}`;
  pill.querySelector(".status-icon").textContent = bot.running ? "✓" : "✕";
  pill.querySelector(".status-text").textContent = bot.running ? `Running · up ${bot.elapsed}` : "Bot is not running";
  document.getElementById("restart-line").textContent = consoleInfo.restartLine
    ? `Last ${consoleInfo.restartLine}`
    : bot.logFile ? `Logging to ${bot.logFile}` : "No logs found";
  const streamingPairs = consoleInfo.streamingPairs;
  const ladders = streamingPairs ? streamingPairs * LADDERS_PER_PAIR : null;
  const synchronized = engine ? engine.synchronizedLadders : null;
  const uptimeKind = feeds.streamingRecentFraction >= 0.95 ? "good" : feeds.streamingRecentFraction >= 0.8 ? "warning" : "critical";
  const feedTile = (venue) => tile({
    label: `${venue} feed`, value: feeds.current[venue] ? "Connected" : "Down", statusKind: feeds.current[venue] ? "good" : "critical",
    detail: bot.running ? "" : "Bot stopped",
  });
  const isFirstDiscovery = bot.running && !streamingPairs;
  document.getElementById("health-tiles").replaceChildren(
    feedTile("Kalshi"),
    feedTile("Polymarket"),
    tile({ label: "Streaming, last 24 h", value: percent(feeds.streamingRecentFraction), statusKind: uptimeKind, detail: "Both feeds connected" }),
    tile({ label: "Pairs streaming", value: isFirstDiscovery ? "Discovering" : count(streamingPairs), detail: `${count(data.coverage.total)} matched pairs saved` }),
    tile({ label: "Synchronized books", value: synchronized !== null && ladders ? percent(synchronized / ladders) : "–",
      detail: synchronized !== null ? `${count(synchronized)} of ${count(ladders)} order books` : "Waiting for engine stats" }),
    tile({ label: "Open opportunities", value: engine ? count(engine.openOpportunities) : "–", detail: engine ? `${count(engine.updates)} book updates this session` : "" }),
    tile({ label: "Disconnects, last 24 h", value: count(feeds.counts.disconnected), detail: "Includes planned stream restarts" }),
    tile({ label: "Sequence gaps, last 24 h", value: count(feeds.counts.sequence_gap), statusKind: feeds.counts.sequence_gap > 0 ? "warning" : "good",
      detail: `${count(feeds.counts.resync_requested)} resyncs, ${count(feeds.counts.queue_overflow)} queue overflows` }),
    tile({ label: "Dropped log records", value: engine ? count(engine.droppedLogRecords) : "–", statusKind: engine && engine.droppedLogRecords > 0 ? "warning" : engine ? "good" : undefined,
      detail: engine ? `Queues: Kalshi ${engine.kalshiQueue}, Polymarket ${engine.polymarketQueue}` : "" }),
    tile({ label: "Discovery cycle", value: count(consoleInfo.discoveryCycle), statusKind: consoleInfo.discoveryFailures > 0 ? "warning" : undefined,
      detail: `${count(consoleInfo.discoveryFailures)} failed fetches since restart` }),
    tile({ label: "Settlement conflicts", value: count(consoleInfo.settlementConflicts), detail: `${count(consoleInfo.excludedBySeries)} excluded by series in the last match` }),
  );

  const hours = data.hours;
  columnChart(document.getElementById("uptime-chart"), hours.map((hour) => ({
    start: hour.start, value: hour.streamingFraction, title: timeLabel(hour.start), detail: `${percent(hour.streamingFraction)} streaming`,
  })), { yFormat: (value) => `${Math.round(value * 100)}%`, title: "Streaming uptime by hour", color: cssVar("--series-1"), fixedMaximum: 1, timeAxisLabels: true, emptyText: "No logs yet." });
  columnChart(document.getElementById("opportunity-chart"), hours.map((hour) => ({
    start: hour.start, value: hour.opportunities, title: timeLabel(hour.start), detail: `${count(hour.opportunities)} opportunities opened`,
  })), { yFormat: (value) => count(value), title: "Opportunities opened by hour", color: cssVar("--series-1"), timeAxisLabels: true, emptyText: "No logs yet." });

  table(document.getElementById("opportunities-table"), [
    { label: "Opened", value: (row) => timeLabel(row.time) },
    { label: "Kalshi market", value: (row) => row.kalshiTicker },
    { label: "Edge", number: true, value: (row) => `${(row.edge * 100).toFixed(1)}¢` },
    { label: "Legs", value: (row) => row.direction.replace("+", " + ").replaceAll("_", " ") },
    { label: "Contracts", number: true, value: (row) => (row.contracts < 1 ? "<1" : count(Math.floor(row.contracts))) },
    { label: "Best profit", number: true, value: (row) => money(row.bestExpectedProfit) },
    { label: "Lasted", number: true, value: (row) => duration(row.durationMs) },
  ], data.opportunities, "No opportunities in the last 48 hours.");

  table(document.getElementById("coverage-table"), [
    { label: "Kalshi series", value: (row) => row[0] },
    { label: "Pairs", number: true, value: (row) => count(row[1]) },
  ], data.coverage.bySeries, "No matched pairs saved.");

  table(document.getElementById("hours-table"), [
    { label: "Hour", value: (row) => timeLabel(row.start) },
    { label: "Streaming", number: true, value: (row) => percent(row.streamingFraction) },
    { label: "Opportunities", number: true, value: (row) => count(row.opportunities) },
  ], [...hours].reverse(), "No data.");
}

function renderExecution(data) {
  const { execution, paper, console: consoleInfo } = data;
  const period = paper.period;
  const trades = paper.trades;
  const unwound = trades.filter((trade) => trade.unwound > 0).length;
  const unhedged = trades.filter((trade) => trade.unhedged > 0).length;
  const lastHalt = [...trades].reverse().find((trade) => trade.halted);
  const halted = Boolean(lastHalt) || Boolean(consoleInfo.halted);
  const tradingKind = execution.mode === "disabled" ? "neutral" : execution.killSwitchEngaged ? "warning" : halted ? "critical" : "good";
  const tradingText = execution.mode === "disabled" ? "Execution off" : execution.killSwitchEngaged ? "Kill switch on" : halted ? "Halted" : "Trading allowed";
  const modeText = { paper: "Paper", live: "Live", disabled: "Disabled" }[execution.mode] || execution.mode;
  document.getElementById("execution-tiles").replaceChildren(
    tile({ label: "Mode", value: modeText, detail: `${money(execution.maximumDollarsPerTrade)} max per trade` }),
    tile({ label: "Trading", value: tradingText, statusKind: tradingKind, detail: lastHalt ? lastHalt.halted : "" }),
    tile({ label: "Kalshi spent this period", value: `${money(period.kalshiSpent)} of ${money(period.kalshiBudget)}`, meter: { share: period.kalshiBudget ? period.kalshiSpent / period.kalshiBudget : 0 },
      detail: `Resets ${timeLabel(period.end)}` }),
    tile({ label: "Polymarket spent this period", value: `${money(period.polymarketSpent)} of ${money(period.polymarketBudget)}`, meter: { share: period.polymarketBudget ? period.polymarketSpent / period.polymarketBudget : 0 },
      detail: `Rebalance every ${execution.rebalanceHours} h` }),
    tile({ label: "Unwound trades", value: count(unwound), statusKind: unwound > 0 ? "warning" : "good", detail: "Second leg missed, first leg flattened" }),
    tile({ label: "Left unhedged", value: count(unhedged), statusKind: unhedged > 0 ? "critical" : "good", detail: "Open exposure that halts trading" }),
  );
  const button = document.getElementById("kill-button");
  button.textContent = execution.killSwitchEngaged ? "Release kill switch" : "Engage kill switch";
  button.classList.toggle("engaged", execution.killSwitchEngaged);
  button.dataset.engaged = execution.killSwitchEngaged ? "true" : "false";
  document.getElementById("kill-detail").textContent = execution.killSwitchEngaged
    ? `${execution.killSwitchPath} exists, so the executor places no new trades.`
    : `Creates ${execution.killSwitchPath}, which stops new trades within one signal.`;
  if (paper.error) {
    document.getElementById("hero-detail").textContent = `Could not settle the journal: ${paper.error}`;
  }
}

async function refresh() {
  document.querySelector(".page").classList.add("loading");
  try {
    const response = await fetch("/api/summary", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    latest = await response.json();
    renderHealth(latest);
    renderPnl(latest);
    renderExecution(latest);
    document.getElementById("updated-at").textContent = `Updated ${new Date(latest.now).toLocaleTimeString()}`;
  } catch (error) {
    document.getElementById("updated-at").textContent = `Update failed: ${error.message}`;
  } finally {
    document.querySelector(".page").classList.remove("loading");
  }
}

document.getElementById("kill-button").addEventListener("click", async (event) => {
  const engage = event.currentTarget.dataset.engaged !== "true";
  if (engage && !window.confirm("Stop the executor from placing new trades?")) {
    return;
  }
  await fetch("/api/kill-switch", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ engaged: engage }) });
  refresh();
});

let resizeTimer = null;
window.addEventListener("resize", () => {
  clearTimeout(resizeTimer);
  resizeTimer = setTimeout(() => latest && (renderHealth(latest), renderPnl(latest), renderExecution(latest)), 150);
});
window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", () => latest && (renderHealth(latest), renderPnl(latest), renderExecution(latest)));

refresh();
setInterval(refresh, REFRESH_MS);
