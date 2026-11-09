import argparse
import collections
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KALSHI_MARKETS_URL = "https://api.elections.kalshi.com/trade-api/v2/markets"
USER_AGENT = "kparb-check"
PAGE_LIMIT = 1000
SECONDS_PER_DAY = 86_400
REQUEST_PAUSE_SECONDS = 0.15
PRIOR_STRENGTH_EVENTS = 200
EVEN_SETTLEMENT = 0.5
EVEN_TOLERANCE = 0.01
EXCLUDED_PREFIXES = ("KXCS2", "KXDOTA2", "KXCODGAME", "KXCOD", "KXR6", "KXLOL", "KXVALORANT")


def fetch_json(url):
    for attempt in range(6):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/json"})
            with urllib.request.urlopen(request, timeout=30) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if error.code not in (429, 500, 502, 503, 504):
                raise
        except OSError:
            pass
        time.sleep(1 + 2 * attempt)
    raise RuntimeError("giving up on " + url)


def winner_series():
    with open(os.path.join(ROOT, "config/entities.json")) as handle:
        leagues = json.load(handle)["leagues"]
    prefixes = set()
    for league in leagues:
        for prefix in league.get("kalshiSeriesPrefixes") or [league["kalshiSeriesPrefix"]]:
            prefixes.add(prefix)
    return sorted(prefixes)


def settled_events(series, since_seconds):
    events = collections.defaultdict(list)
    cursor = ""
    while True:
        query = {"series_ticker": series, "status": "settled", "limit": PAGE_LIMIT, "min_close_ts": since_seconds}
        if cursor:
            query["cursor"] = cursor
        page = fetch_json(KALSHI_MARKETS_URL + "?" + urllib.parse.urlencode(query))
        for market in page.get("markets", []):
            value = market.get("settlement_value_dollars")
            events[market["event_ticker"]].append((market.get("result") or "", float(value) if value not in (None, "") else None))
        cursor = page.get("cursor") or ""
        time.sleep(REQUEST_PAUSE_SECONDS)
        if not cursor or not page.get("markets"):
            return events


def main():
    parser = argparse.ArgumentParser(description="Measure how often Kalshi game markets settle at a fair price (no result) by league")
    parser.add_argument("--days", type=int, default=180)
    parser.add_argument("--prior", type=float, default=0.005, help="cancellation probability assumed before seeing data")
    parser.add_argument("--minimum-events", type=int, default=30)
    parser.add_argument("--write-config", action="store_true", help="store the estimates in config/config.json")
    arguments = parser.parse_args()

    since = int(time.time()) - arguments.days * SECONDS_PER_DAY
    smoothed = lambda count, total: (count + arguments.prior * PRIOR_STRENGTH_EVENTS) / (total + PRIOR_STRENGTH_EVENTS)
    any_estimates, uneven_estimates = {}, {}
    pooled = collections.Counter()
    print(f"{'league prefix':<22} {'events':>7} {'no result':>9} {'uneven':>7} {'any est':>8} {'uneven est':>10}")
    for prefix in winner_series():
        events = {}
        for suffix in ("GAME", "MATCH"):
            events.update(settled_events(prefix + suffix, since))
        if not events:
            continue
        no_result = uneven = 0
        for markets in events.values():
            scalars = [value for result, value in markets if result not in ("yes", "no")]
            if scalars:
                no_result += 1
                if any(value is None or abs(value - EVEN_SETTLEMENT) > EVEN_TOLERANCE for value in scalars):
                    uneven += 1
        if not prefix.startswith(EXCLUDED_PREFIXES):
            pooled.update({"events": len(events), "no_result": no_result, "uneven": uneven})
        any_estimate, uneven_estimate = smoothed(no_result, len(events)), smoothed(uneven, len(events))
        print(f"{prefix:<22} {len(events):>7} {no_result:>9} {uneven:>7} {any_estimate:>8.2%} {uneven_estimate:>10.2%}")
        if len(events) >= arguments.minimum_events:
            any_estimates[prefix] = round(any_estimate, 4)
            uneven_estimates[prefix] = round(uneven_estimate, 4)
    pooled_any = smoothed(pooled["no_result"], pooled["events"])
    pooled_uneven = smoothed(pooled["uneven"], pooled["events"])
    print(f"\nall leagues except the excluded esports series: {pooled['events']} events, {pooled['no_result']} settled without a winner, {pooled['uneven']} of those away from 50/50; "
          f"pooled estimates {pooled_any:.2%} any, {pooled_uneven:.2%} uneven")

    if arguments.write_config:
        path = os.path.join(ROOT, "config/config.json")
        with open(path) as handle:
            config = json.load(handle)
        matching = config.setdefault("matching", {})
        matching["defaultNoResultProbability"] = round(pooled_any, 4)
        matching["noResultProbabilityByKalshiPrefix"] = any_estimates
        matching["defaultUnevenNoResultProbability"] = round(pooled_uneven, 4)
        matching["unevenNoResultProbabilityByKalshiPrefix"] = uneven_estimates
        with open(path, "w") as handle:
            json.dump(config, handle, indent=2)
            handle.write("\n")
        print(f"wrote estimates for {len(any_estimates)} leagues to config/config.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
