import argparse
import collections
import csv
import datetime
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from simulate_pnl import NANOS_PER_SECOND, kalshi_settlement, polymarket_settlement

KALSHI_TAKER_FEE_RATE = 0.07
CENTS_PER_DOLLAR = 100


def kalshi_fee(contracts, price):
    return math.ceil(KALSHI_TAKER_FEE_RATE * contracts * price * (1.0 - price) * CENTS_PER_DOLLAR - 1e-9) / CENTS_PER_DOLLAR


def polymarket_fee(contracts, price, rate, exponent):
    return contracts * rate * (price * (1.0 - price)) ** exponent if rate > 0 else 0.0


def load_trades(path):
    trades = []
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if line:
                trade = json.loads(line)
                if trade.get("fills"):
                    trades.append(trade)
    return trades


def load_settlements(trades, cache_path):
    cache = {"kalshi": {}, "polymarket": {}}
    if os.path.exists(cache_path):
        with open(cache_path) as handle:
            cache = json.load(handle)
    tickers = {fill["market"] for trade in trades for fill in trade["fills"] if fill["venue"] == "kalshi"}
    slugs = {trade["polymarket_slug"] for trade in trades}
    for ticker in sorted(tickers):
        if cache["kalshi"].get(ticker, {}).get("yes_value") is None:
            cache["kalshi"][ticker] = kalshi_settlement(ticker)
    for slug in sorted(slugs):
        if cache["polymarket"].get(slug, {}).get("prices") is None:
            cache["polymarket"][slug] = polymarket_settlement(slug)
    os.makedirs(os.path.dirname(cache_path) or ".", exist_ok=True)
    with open(cache_path, "w") as handle:
        json.dump(cache, handle, indent=1)
    return cache


def fill_value(fill, trade, settlements):
    if fill["venue"] == "kalshi":
        yes_value = settlements["kalshi"].get(fill["market"], {}).get("yes_value")
        if yes_value is None:
            return None
        return yes_value if fill["side"] == "yes" else 1.0 - yes_value
    prices = settlements["polymarket"].get(trade["polymarket_slug"], {}).get("prices")
    return None if prices is None else prices[fill["outcome_index"]]


def fill_cost(fill, trade):
    notional = fill["contracts"] * fill["price"]
    if fill["venue"] == "kalshi":
        return notional + kalshi_fee(fill["contracts"], fill["price"])
    return notional + polymarket_fee(fill["contracts"], fill["price"], trade.get("polymarket_fee_rate", 0.0),
                                     trade.get("polymarket_fee_exponent", 1.0))


def evaluate(trade, settlements):
    cost = sum(fill_cost(fill, trade) for fill in trade["fills"])
    values = [fill_value(fill, trade, settlements) for fill in trade["fills"]]
    hedged_pairs = trade.get("second_filled", 0) + trade.get("chased", 0) + trade.get("unwound", 0)
    expected = hedged_pairs * 1.0 - cost
    if any(value is None for value in values):
        return cost, None, expected, None
    payout = sum(fill["contracts"] * value for fill, value in zip(trade["fills"], values))
    payout_per_pair = payout / hedged_pairs if hedged_pairs else None
    return cost, payout - cost, expected, payout_per_pair


def local_day(trade):
    return datetime.datetime.fromtimestamp(trade["wall_time_ns"] / NANOS_PER_SECOND).strftime("%a %b %d")


def main():
    parser = argparse.ArgumentParser(description="Settle the paper executor's journal against real results")
    parser.add_argument("--journal", default="data/executions.jsonl")
    parser.add_argument("--since", help="only count trades after this local time, e.g. 2026-11-09T12:00")
    parser.add_argument("--output", default="data/paper")
    parser.add_argument("--json", action="store_true", help="print the settled trades and totals as JSON for the dashboard")
    arguments = parser.parse_args()

    if not os.path.exists(arguments.journal):
        if arguments.json:
            print(json.dumps({"trades": [], "totals": {}}))
            return 0
        print(f"no journal at {arguments.journal} yet")
        return 1
    trades = load_trades(arguments.journal)
    if arguments.since:
        since_ns = int(datetime.datetime.fromisoformat(arguments.since).timestamp() * NANOS_PER_SECOND)
        trades = [trade for trade in trades if trade["wall_time_ns"] >= since_ns]
    if not trades:
        if arguments.json:
            print(json.dumps({"trades": [], "totals": {}}))
            return 0
        print("no filled paper trades yet")
        return 1
    settlements = load_settlements(trades, os.path.join(arguments.output, "settlements.json"))

    totals = collections.Counter()
    by_day = collections.defaultdict(collections.Counter)
    rows = []
    for trade in trades:
        cost, realized, expected, payout_per_pair = evaluate(trade, settlements)
        day = by_day[local_day(trade)]
        for bucket in (totals, day):
            bucket["trades"] += 1
            bucket["spent"] += cost
            if realized is None:
                bucket["pending"] += 1
                bucket["pending_expected"] += expected
            else:
                bucket["settled"] += 1
                bucket["realized"] += realized
                if payout_per_pair is not None and abs(payout_per_pair - 1.0) > 1e-6:
                    bucket["mismatched"] += 1
            bucket["unwound"] += trade.get("unwound", 0) > 0
            bucket["unhedged"] += trade.get("unhedged", 0) > 0
        rows.append((trade, cost, realized, expected, payout_per_pair))

    if arguments.json:
        print(json.dumps({
            "totals": dict(totals),
            "trades": [{
                "time_ns": trade["wall_time_ns"], "kalshi_ticker": trade["kalshi_ticker"], "polymarket_slug": trade["polymarket_slug"],
                "direction": trade["direction"], "contracts": trade.get("first_filled", 0), "cost": cost, "realized": realized,
                "expected": expected, "payout_per_pair": payout_per_pair, "chased": trade.get("chased", 0),
                "unwound": trade.get("unwound", 0), "unhedged": trade.get("unhedged", 0),
                "kalshi_dollars": trade.get("kalshi_dollars", 0.0), "polymarket_dollars": trade.get("polymarket_dollars", 0.0),
                "halted": trade.get("halted", ""),
            } for trade, cost, realized, expected, payout_per_pair in rows],
        }))
        return 0

    print(f"{'day':<11} {'trades':>6} {'settled':>7} {'realized':>9} {'pending':>7} {'pending_exp':>11} {'spent':>9} {'mismatch':>8}")
    for name, bucket in list(by_day.items()) + [("total", totals)]:
        print(f"{name:<11} {bucket['trades']:>6} {bucket['settled']:>7} {bucket['realized']:>9.2f} {bucket['pending']:>7} "
              f"{bucket['pending_expected']:>11.2f} {bucket['spent']:>9.2f} {bucket['mismatched']:>8}")
    total = totals["realized"] + totals["pending_expected"]
    print(f"\npaper P&L: ${totals['realized']:.2f} realized + ${totals['pending_expected']:.2f} expected on pending = ${total:.2f} "
          f"on ${totals['spent']:.2f} traded; {totals['unwound']} trades unwound, {totals['unhedged']} left unhedged")

    os.makedirs(arguments.output, exist_ok=True)
    with open(os.path.join(arguments.output, "paper_trades.csv"), "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["time", "kalshi_ticker", "polymarket_slug", "direction", "contracts", "cost", "realized", "expected",
                         "payout_per_pair", "unwound", "unhedged"])
        for trade, cost, realized, expected, payout_per_pair in rows:
            writer.writerow([datetime.datetime.fromtimestamp(trade["wall_time_ns"] / NANOS_PER_SECOND).isoformat(timespec="seconds"),
                             trade["kalshi_ticker"], trade["polymarket_slug"], trade["direction"], trade.get("first_filled", 0),
                             f"{cost:.2f}", "" if realized is None else f"{realized:.2f}", f"{expected:.2f}",
                             "" if payout_per_pair is None else f"{payout_per_pair:.3f}", trade.get("unwound", 0), trade.get("unhedged", 0)])
    return 0


if __name__ == "__main__":
    sys.exit(main())
