import argparse
import json
import time
import urllib.error
import urllib.parse
import urllib.request

USER_AGENT = "kparb-snapshot"
KALSHI_ORDERBOOK_URL = "https://api.elections.kalshi.com/trade-api/v2/markets/{ticker}/orderbook"
POLYMARKET_BOOK_URL = "https://clob.polymarket.com/book?token_id={token}"
KALSHI_TAKER_FEE_RATE = 0.07


def fetch_json(url):
    for attempt in range(6):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(request, timeout=30) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return None
            if error.code not in (429, 500, 502, 503, 504):
                raise
            time.sleep(1 + attempt)
        except OSError:
            time.sleep(1 + attempt)
    return None


def kalshi_levels(book, side):
    levels = book.get(side + "_dollars")
    if levels is not None:
        return [(float(price), float(quantity)) for price, quantity in levels]
    return [(price / 100.0, float(quantity)) for price, quantity in book.get(side) or []]


def kalshi_asks(ticker):
    document = fetch_json(KALSHI_ORDERBOOK_URL.format(ticker=urllib.parse.quote(ticker)))
    if not document:
        return None, None
    book = document.get("orderbook_fp") or document.get("orderbook") or {}
    yes_bids = kalshi_levels(book, "yes")
    no_bids = kalshi_levels(book, "no")
    best_yes_bid = max(yes_bids, default=None)
    best_no_bid = max(no_bids, default=None)
    yes_ask = (round(1.0 - best_no_bid[0], 4), best_no_bid[1]) if best_no_bid else None
    no_ask = (round(1.0 - best_yes_bid[0], 4), best_yes_bid[1]) if best_yes_bid else None
    return yes_ask, no_ask


def polymarket_ask(token):
    document = fetch_json(POLYMARKET_BOOK_URL.format(token=token))
    if not document:
        return None
    asks = [(float(level["price"]), float(level["size"])) for level in document.get("asks") or []]
    return min(asks, default=None)


def kalshi_fee(price):
    return KALSHI_TAKER_FEE_RATE * price * (1.0 - price)


def polymarket_fee(price, rate, exponent):
    return rate * (price * (1.0 - price)) ** exponent


def hedge(kalshi_leg, polymarket_leg, pair):
    if not kalshi_leg or not polymarket_leg:
        return None
    kalshi_price, kalshi_size = kalshi_leg
    polymarket_price, polymarket_size = polymarket_leg
    fees = kalshi_fee(kalshi_price) + polymarket_fee(polymarket_price, pair.get("polymarketTakerFeeRate", 0.0),
                                                     pair.get("polymarketTakerFeeExponent", 1.0))
    cost = kalshi_price + polymarket_price + fees
    return {"kalshi": kalshi_price, "polymarket": polymarket_price, "fees": round(fees, 4), "cost": round(cost, 4),
            "edge": round(1.0 - cost, 4), "size": min(kalshi_size, polymarket_size)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pairs", default="data/matched_pairs.json")
    arguments = parser.parse_args()
    with open(arguments.pairs) as file:
        pairs = json.load(file)["pairs"]
    results = []
    for pair in pairs:
        yes_ask, no_ask = kalshi_asks(pair["kalshiTicker"])
        matching_token, opposite_token = pair["polymarketYesToken"], pair["polymarketNoToken"]
        if pair.get("isInverted"):
            matching_token, opposite_token = opposite_token, matching_token
        legs = [("kalshi YES + polymarket opposite", hedge(yes_ask, polymarket_ask(opposite_token), pair)),
                ("kalshi NO + polymarket matching", hedge(no_ask, polymarket_ask(matching_token), pair))]
        legs = [(name, leg) for name, leg in legs if leg]
        best = min(legs, key=lambda item: item[1]["cost"]) if legs else None
        results.append((pair, best))
    results.sort(key=lambda item: item[1][1]["cost"] if item[1] else 99.0)
    print(f"{'cost':>6} {'edge':>7} {'size':>8}  pair")
    for pair, best in results:
        if best is None:
            print(f"{'-':>6} {'-':>7} {'-':>8}  {pair['kalshiTicker']} <-> {pair['polymarketSlug']} (one side has no asks)")
            continue
        name, leg = best
        print(f"{leg['cost']:6.3f} {leg['edge']:+7.3f} {leg['size']:8.0f}  {pair['kalshiTicker']} <-> {pair['polymarketSlug']}"
              f"  [{name}: {leg['kalshi']:.3f} + {leg['polymarket']:.3f} + fees {leg['fees']:.3f}]")
    opportunities = [item for item in results if item[1] and item[1][1]["edge"] > 0]
    print(f"\n{len(pairs)} pairs, {len(opportunities)} with a positive edge after taker fees at the top of book")


if __name__ == "__main__":
    main()
