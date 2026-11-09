#!/usr/bin/env python3
import argparse
import bisect
import csv
import datetime
import glob
import heapq
import io
import json
import os
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import defaultdict
from dataclasses import dataclass, field

KALSHI_MARKET_URL = "https://api.elections.kalshi.com/trade-api/v2/markets/"
POLYMARKET_MARKETS_URL = "https://gamma-api.polymarket.com/markets"
USER_AGENT = "kparb-sim"
HEADER_FORMAT = "<QIIqq"
NANOS_PER_SECOND = 1_000_000_000
NANOS_PER_MILLISECOND = 1_000_000
SNAPSHOT_SETTLE_NS = NANOS_PER_MILLISECOND


@dataclass
class Quote:
    time_ns: int
    kalshi_price: float
    polymarket_price: float
    edge: float
    contracts: float
    profit: float
    is_inverted: bool

    @property
    def profit_per_contract(self):
        return self.profit / self.contracts if self.contracts > 0 else 0.0


@dataclass
class Episode:
    kalshi_ticker: str
    polymarket_slug: str
    direction: str
    log_file: str
    quotes: list = field(default_factory=list)
    end_ns: int = 0

    @property
    def start_ns(self):
        return self.quotes[0].time_ns

    def quote_at(self, time_ns):
        times = [quote.time_ns for quote in self.quotes]
        return self.quotes[bisect.bisect_right(times, time_ns) - 1]

    @property
    def kalshi_side(self):
        return "yes" if self.direction.startswith("kalshi_yes") else "no"

    @property
    def polymarket_outcome_index(self):
        engine_side_is_yes = self.direction.endswith("polymarket_yes")
        inverted = self.quotes[0].is_inverted
        return 0 if engine_side_is_yes != inverted else 1


@dataclass
class Trade:
    episode: Episode
    time_ns: int
    contracts: int
    profit_per_contract: float
    edge: float

    @property
    def cost(self):
        return self.contracts * (1.0 - self.profit_per_contract)

    @property
    def expected_profit(self):
        return self.contracts * self.profit_per_contract


def read_anchors(path):
    with open(path, "rb") as handle:
        _, _, _, wall_anchor, steady_anchor = struct.unpack(HEADER_FORMAT, handle.read(struct.calcsize(HEADER_FORMAT)))
    return wall_anchor, steady_anchor


def decode(decoder, path, record_type):
    output = subprocess.run([decoder, path, "--type", record_type, "--csv"], capture_output=True, text=True, check=True).stdout
    return list(csv.DictReader(io.StringIO(output)))


def load_episodes(decoder, path):
    wall_anchor, steady_anchor = read_anchors(path)
    to_wall = lambda steady: wall_anchor + int(steady) - steady_anchor
    events = []
    for row in decode(decoder, path, "opportunity"):
        events.append((to_wall(row["timestamp_ns"]), 0, row))
    for row in decode(decoder, path, "opportunity_closed"):
        events.append((to_wall(row["timestamp_ns"]), 1, row))
    events.sort(key=lambda event: (event[0], event[1]))
    open_episodes = {}
    finished = []
    last_time = 0
    for time_ns, kind, row in events:
        last_time = time_ns
        key = (row["kalshi_ticker"], row["polymarket_identifier"], row["direction"])
        if kind == 1:
            episode = open_episodes.pop(key, None)
            if episode:
                episode.end_ns = time_ns
                finished.append(episode)
            continue
        episode = open_episodes.get(key)
        if episode is None:
            episode = Episode(key[0], key[1], key[2], os.path.basename(path))
            open_episodes[key] = episode
        episode.quotes.append(Quote(time_ns, float(row["kalshi_price"]), float(row["polymarket_price"]), float(row["edge"]),
                                    float(row["contracts"]), float(row["expected_profit"]), row.get("is_inverted") == "1"))
    for episode in open_episodes.values():
        episode.end_ns = last_time
        finished.append(episode)
    return [episode for episode in finished if episode.quotes]


def simulate(episodes, delay_ns, settlements, now_ns, bankroll_dollars, maximum_trade_dollars):
    trades = []
    taken_by_book = defaultdict(float)
    open_positions = []
    deployed = 0.0
    for episode in sorted(episodes, key=lambda item: item.start_ns):
        decided = episode.start_ns + SNAPSHOT_SETTLE_NS
        arrival = decided + delay_ns
        if arrival > episode.end_ns:
            continue
        while open_positions and open_positions[0][0] <= arrival:
            deployed -= heapq.heappop(open_positions)[1]
        books = [("kalshi", episode.kalshi_ticker, episode.kalshi_side), ("polymarket", episode.polymarket_slug, episode.polymarket_outcome_index)]
        seen = episode.quote_at(decided)
        found = episode.quote_at(arrival)
        profit_per_contract = min(seen.profit_per_contract, found.profit_per_contract)
        if profit_per_contract <= 0:
            continue
        contracts = min(seen.contracts, found.contracts) - max(taken_by_book[book] for book in books)
        cost_per_contract = 1.0 - profit_per_contract
        if maximum_trade_dollars > 0:
            contracts = min(contracts, maximum_trade_dollars / cost_per_contract)
        if bankroll_dollars > 0:
            contracts = min(contracts, (bankroll_dollars - deployed) / cost_per_contract)
        whole_contracts = int(contracts)
        if whole_contracts < 1:
            continue
        trade = Trade(episode, arrival, whole_contracts, profit_per_contract, min(seen.edge, found.edge))
        trades.append(trade)
        for book in books:
            taken_by_book[book] += whole_contracts
        heapq.heappush(open_positions, (settle(trade, settlements, now_ns)[1], trade.cost))
        deployed += trade.cost
    return trades


def fetch_json(url):
    for attempt in range(4):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/json"})
            with urllib.request.urlopen(request, timeout=20) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return None
            time.sleep(1 + attempt)
        except OSError:
            time.sleep(1 + attempt)
    return None


def parse_time(text):
    if not text:
        return None
    text = text.replace("Z", "+00:00")
    if "+" not in text[10:] and text.count("-") <= 2:
        text += "+00:00"
    try:
        return int(datetime.datetime.fromisoformat(text).timestamp() * NANOS_PER_SECOND)
    except ValueError:
        return None


def kalshi_settlement(ticker):
    data = fetch_json(KALSHI_MARKET_URL + urllib.parse.quote(ticker))
    market = (data or {}).get("market") or {}
    result = market.get("result") or ""
    yes_value = None
    if market.get("status") in ("finalized", "settled"):
        if result in ("yes", "no"):
            yes_value = 1.0 if result == "yes" else 0.0
        elif result == "scalar" and market.get("settlement_value_dollars") is not None:
            yes_value = float(market["settlement_value_dollars"])
    return {"yes_value": yes_value, "result": result,
            "settled_ns": parse_time(market.get("settlement_ts")) or parse_time(market.get("close_time"))}


def polymarket_settlement(slug):
    markets = fetch_json(f"{POLYMARKET_MARKETS_URL}?slug={urllib.parse.quote(slug)}&closed=true") or []
    if not markets:
        markets = fetch_json(f"{POLYMARKET_MARKETS_URL}?slug={urllib.parse.quote(slug)}") or []
    if not markets:
        return {"prices": None, "settled_ns": None}
    market = markets[0]
    resolved = market.get("closed") and market.get("umaResolutionStatus") == "resolved"
    prices = [float(price) for price in json.loads(market.get("outcomePrices") or "[]")] if resolved else None
    return {"prices": prices, "settled_ns": parse_time(market.get("closedTime")) or parse_time(market.get("endDate"))}


def load_settlements(episodes, cache_path):
    cache = {"kalshi": {}, "polymarket": {}}
    if os.path.exists(cache_path):
        with open(cache_path) as handle:
            cache = json.load(handle)
    for ticker in sorted({episode.kalshi_ticker for episode in episodes}):
        if cache["kalshi"].get(ticker, {}).get("yes_value") is None:
            cache["kalshi"][ticker] = kalshi_settlement(ticker)
    for slug in sorted({episode.polymarket_slug for episode in episodes}):
        if cache["polymarket"].get(slug, {}).get("prices") is None:
            cache["polymarket"][slug] = polymarket_settlement(slug)
    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    with open(cache_path, "w") as handle:
        json.dump(cache, handle, indent=1)
    return cache


def settle(trade, settlements, now_ns):
    kalshi = settlements["kalshi"].get(trade.episode.kalshi_ticker, {})
    polymarket = settlements["polymarket"].get(trade.episode.polymarket_slug, {})
    settled_ns = max(value for value in (kalshi.get("settled_ns"), polymarket.get("settled_ns"), trade.time_ns) if value)
    if kalshi.get("yes_value") is None or polymarket.get("prices") is None:
        return None, now_ns
    kalshi_value = kalshi["yes_value"] if trade.episode.kalshi_side == "yes" else 1.0 - kalshi["yes_value"]
    polymarket_value = polymarket["prices"][trade.episode.polymarket_outcome_index]
    return kalshi_value + polymarket_value, max(settled_ns, trade.time_ns)


def peak_capital(intervals):
    changes = sorted([(start, cost) for start, _, cost in intervals] + [(end, -cost) for _, end, cost in intervals],
                     key=lambda change: (change[0], change[1]))
    deployed = peak = 0.0
    for _, change in changes:
        deployed += change
        peak = max(peak, deployed)
    return peak


def summarize(trades, settlements, now_ns, suspect_edge):
    summary = defaultdict(float)
    intervals = []
    rows = []
    for trade in trades:
        payout, settled_ns = settle(trade, settlements, now_ns)
        intervals.append((trade.time_ns, settled_ns, trade.cost))
        suspect = trade.edge >= suspect_edge
        summary["trades"] += 1
        summary["contracts"] += trade.contracts
        summary["expected"] += trade.expected_profit
        if payout is None:
            summary["pending"] += 1
            summary["pending_expected"] += trade.expected_profit
            realized = None
        else:
            realized = trade.contracts * payout - trade.cost
            summary["realized"] += realized
            if abs(payout - 1.0) > 1e-9:
                summary["mismatched"] += 1
        if suspect:
            summary["suspect"] += 1
            summary["suspect_profit"] += realized if realized is not None else trade.expected_profit
        rows.append((trade, payout, realized, suspect))
    summary["peak_capital"] = peak_capital(intervals)
    return summary, rows


def format_time(time_ns):
    return datetime.datetime.fromtimestamp(time_ns / NANOS_PER_SECOND).strftime("%a %b %d %H:%M:%S")


def main():
    parser = argparse.ArgumentParser(description="Paper-trade logged arbitrage opportunities and settle them against real results")
    parser.add_argument("--logs", default="data/logs/*.bin")
    parser.add_argument("--decoder", default="build/release/src/tools/kparb_logdecode")
    parser.add_argument("--since", help="only replay logs written after this local time, e.g. 2026-11-05T17:00")
    parser.add_argument("--delays-ms", default="0,50,100,250,500,1000,2000,5000")
    parser.add_argument("--bankroll-dollars", type=float, default=0.0)
    parser.add_argument("--maximum-trade-dollars", type=float, default=0.0)
    parser.add_argument("--exclude-series", default="", help="comma-separated Kalshi series prefixes to skip, e.g. KXCS2,KXDOTA2")
    parser.add_argument("--suspect-edge", type=float, default=0.20)
    parser.add_argument("--detail-delay-ms", type=int, default=250)
    parser.add_argument("--output", default="data/sim")
    arguments = parser.parse_args()

    since_ns = int(datetime.datetime.fromisoformat(arguments.since).timestamp() * NANOS_PER_SECOND) if arguments.since else 0
    episodes = []
    for path in sorted(glob.glob(arguments.logs)):
        if read_anchors(path)[0] >= since_ns:
            episodes.extend(load_episodes(arguments.decoder, path))
    excluded = tuple(prefix for prefix in arguments.exclude_series.split(",") if prefix)
    if excluded:
        episodes = [episode for episode in episodes if not episode.kalshi_ticker.startswith(excluded)]
    if not episodes:
        print("no opportunities in the selected logs")
        return 1
    print(f"replaying {len(episodes)} opportunity episodes from {format_time(min(e.start_ns for e in episodes))} "
          f"to {format_time(max(e.end_ns for e in episodes))}")

    delays = [int(value) for value in arguments.delays_ms.split(",")]
    if arguments.detail_delay_ms not in delays:
        delays.append(arguments.detail_delay_ms)
    settlements = load_settlements(episodes, os.path.join(arguments.output, "settlements.json"))
    now_ns = time.time_ns()
    runs = {delay: simulate(episodes, delay * NANOS_PER_MILLISECOND, settlements, now_ns, arguments.bankroll_dollars,
                            arguments.maximum_trade_dollars) for delay in delays}

    print(f"bankroll=${arguments.bankroll_dollars:g} maximum_trade=${arguments.maximum_trade_dollars:g} (0 = unlimited) "
          f"suspect_edge>={arguments.suspect_edge:.2f}")
    print(f"{'delay_ms':>8} {'trades':>6} {'contracts':>10} {'expected':>10} {'realized':>10} {'pending':>8} "
          f"{'mismatch':>8} {'suspect':>8} {'clean_pnl':>10} {'peak_cap':>10} {'return':>7}")
    os.makedirs(arguments.output, exist_ok=True)
    for delay in sorted(delays):
        summary, rows = summarize(runs[delay], settlements, now_ns, arguments.suspect_edge)
        clean = summary["realized"] + summary["pending_expected"] - summary["suspect_profit"]
        capital = summary["peak_capital"]
        print(f"{delay:>8} {int(summary['trades']):>6} {summary['contracts']:>10.0f} {summary['expected']:>10.2f} "
              f"{summary['realized']:>10.2f} {int(summary['pending']):>8} {int(summary['mismatched']):>8} "
              f"{int(summary['suspect']):>8} {clean:>10.2f} {capital:>10.0f} {clean / capital if capital else 0:>7.2%}")
        with open(os.path.join(arguments.output, f"trades-{delay}ms.csv"), "w", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(["time", "kalshi_ticker", "polymarket_slug", "direction", "contracts", "edge", "cost", "expected_profit",
                             "payout_per_contract", "realized_profit", "suspect", "log_file"])
            for trade, payout, realized, suspect in rows:
                writer.writerow([format_time(trade.time_ns), trade.episode.kalshi_ticker, trade.episode.polymarket_slug,
                                 trade.episode.direction, trade.contracts, f"{trade.edge:.3f}", f"{trade.cost:.2f}",
                                 f"{trade.expected_profit:.2f}", "" if payout is None else f"{payout:.3f}",
                                 "" if realized is None else f"{realized:.2f}", int(suspect), trade.episode.log_file])

    summary, rows = summarize(runs[arguments.detail_delay_ms], settlements, now_ns, arguments.suspect_edge)
    print(f"\nlargest trades at {arguments.detail_delay_ms} ms:")
    for trade, payout, realized, suspect in sorted(rows, key=lambda row: -(row[2] if row[2] is not None else row[0].expected_profit))[:15]:
        outcome = "pending" if payout is None else f"paid {payout:.2f}/contract"
        flag = " SUSPECT" if suspect else ""
        value = realized if realized is not None else trade.expected_profit
        print(f"  {value:>9.2f}  {trade.contracts:>6} @ edge {trade.edge:.3f}  {format_time(trade.time_ns)}  "
              f"{trade.episode.kalshi_ticker} {trade.episode.direction} {outcome}{flag}")
    mismatches = [row for row in rows if row[1] is not None and abs(row[1] - 1.0) > 1e-9]
    if mismatches:
        print(f"\nsettlement mismatches at {arguments.detail_delay_ms} ms:")
        for trade, payout, realized, _ in mismatches:
            print(f"  {realized:>9.2f}  {trade.episode.kalshi_ticker} <-> {trade.episode.polymarket_slug} {trade.episode.direction} paid {payout:.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
