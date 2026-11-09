import argparse
import datetime
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_EXECUTION = {"maximumDollarsPerTrade": 10.0, "kalshiBudgetDollars": 250.0, "polymarketBudgetDollars": 250.0, "rebalanceHours": 24.0,
                     "journalPath": "data/executions.jsonl"}
REACTION_DELAYS_MS = [50, 250, 1000]
NANOS_PER_SECOND = 1_000_000_000


def execution_config():
    try:
        with open(os.path.join(ROOT, "config/config.json")) as handle:
            configured = json.load(handle).get("execution", {})
    except (OSError, ValueError):
        configured = {}
    return {**DEFAULT_EXECUTION, **configured}


def run(arguments):
    return subprocess.run([sys.executable] + arguments, cwd=ROOT, capture_output=True, text=True, check=True).stdout


def main():
    parser = argparse.ArgumentParser(description="Compare the paper executor's P&L with the simulator over the same window")
    parser.add_argument("--days", type=float, default=7.0)
    arguments = parser.parse_args()
    execution = execution_config()
    since = datetime.datetime.now() - datetime.timedelta(days=arguments.days)
    since_text = since.strftime("%Y-%m-%dT%H:%M")

    paper = json.loads(run(["scripts/paper_pnl.py", "--journal", execution["journalPath"], "--since", since_text, "--json"]))
    totals = paper.get("totals", {})
    paper_total = totals.get("realized", 0.0) + totals.get("pending_expected", 0.0)

    bankroll = execution["kalshiBudgetDollars"] + execution["polymarketBudgetDollars"]
    with tempfile.TemporaryDirectory() as scratch:
        summary_path = os.path.join(scratch, "summary.json")
        simulation_logs = subprocess.run(
            [sys.executable, "scripts/simulate_pnl.py", "--since", since_text, "--delays-ms", ",".join(map(str, REACTION_DELAYS_MS)),
             "--detail-delay-ms", str(REACTION_DELAYS_MS[1]), "--bankroll-dollars", str(bankroll),
             "--maximum-trade-dollars", str(execution["maximumDollarsPerTrade"]), "--rebalance-hours", str(execution["rebalanceHours"]),
             "--output", os.path.join(ROOT, "data/sim/weekly"), "--summary-json", summary_path],
            cwd=ROOT, capture_output=True, text=True)
        simulated = json.load(open(summary_path)) if os.path.exists(summary_path) else None

    lines = [f":clipboard: *KPArb weekly report* ({since:%b %d} to {datetime.datetime.now():%b %d})",
             f"Paper executor: {money(paper_total)} on {totals.get('trades', 0)} trades "
             f"({money(totals.get('realized', 0.0))} settled, {totals.get('pending', 0)} pending, {totals.get('mismatched', 0)} settlement mismatches, "
             f"{totals.get('unwound', 0)} unwound, {totals.get('unhedged', 0)} unhedged)"]
    if simulated:
        lines.append(f"Simulator, same limits (${execution['maximumDollarsPerTrade']:g} a trade, ${bankroll:g} bankroll, "
                     f"rebalance every {execution['rebalanceHours']:g} h), {simulated['episodes']} opportunities:")
        for delay in REACTION_DELAYS_MS:
            row = simulated["delays"].get(str(delay)) or simulated["delays"].get(delay)
            if row:
                lines.append(f"  {delay} ms reaction: {money(row['clean_pnl'])} on {int(row['trades'])} trades "
                             f"({int(row.get('mismatched', 0))} mismatches, {int(row.get('pending', 0))} pending)")
        reference = simulated["delays"].get(str(REACTION_DELAYS_MS[1]))
        if reference:
            gap = paper_total - reference["clean_pnl"]
            lines.append(f"Paper minus simulator at {REACTION_DELAYS_MS[1]} ms: {money(gap)}. Paper fills are optimistic, so a large positive gap "
                         "means the paper numbers overstate what live trading would earn.")
    else:
        lines.append("Simulator found no opportunities in this window." if "no opportunities" in simulation_logs.stdout
                     else f"Simulator failed: {(simulation_logs.stderr or simulation_logs.stdout)[-400:]}")
    print("\n".join(lines))
    return 0


def money(value):
    return f"{'-' if value < 0 else '+'}${abs(value):.2f}"


if __name__ == "__main__":
    sys.exit(main())
