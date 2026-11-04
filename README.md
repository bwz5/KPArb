# KPArb

Detects arbitrage between equivalent Kalshi and Polymarket binary markets.

## How it works

1. **Ingestion** (`src/ingestion`) downloads every open binary market from the Kalshi REST API and the Polymarket Gamma API. It then streams order books over the Kalshi WebSocket (signed with your API key) and the Polymarket market WebSocket.
2. **Matching** (`src/matching`) builds candidate pairs, then has an open-weights LLM confirm them. The LLM is reached through an OpenAI-compatible API (Groq `openai/gpt-oss-120b` by default). It confirms that each pair settles on the same outcome, and which way round the outcomes line up. Every verdict is cached in `data/match_cache.json`.
   - **Text.** A TF-IDF index compares title, subtitle and context. For Kalshi the context is `rules_primary`, which names both teams. For Polymarket it is the event title plus the first paragraph of the description.
   - **Structure.** Candidates must pass these structural filters:
     - Market type: a Kalshi `…GAME`/`MATCH` series pairs only with a Polymarket `moneyline`, and likewise `SPREAD`, `TOTAL`, `TEAMTOTAL`, `SCORE` and `BTTS`, including quarter and half variants.
     - Strike: Kalshi `floor_strike`/`cap_strike` must equal Polymarket `line`, and a Polymarket line needs a Kalshi strike.
     - Subject team: a spread or team total must be about the same team.
     - Close times must fall within `maximumCloseTimeDifferenceDays`, and the numbers in the titles must agree.
   - **Entities.** `config/entities.json` lists about 2,350 teams in 95 leagues. Each one has a Kalshi code (from tickers such as `KXNHLGAME-26NOV04CGYSEA`), Kalshi's name ("Calgary"), Polymarket's abbreviation and name from the Gamma `/teams` list ("cal", "Flames"), and one standard name ("Calgary Flames"). Before matching, every alias in a market's title, subtitle, event title, rules and outcomes is replaced with the standard name, so both venues name the same entity the same way. Leagues are scoped by the longest Kalshi series prefix or Polymarket slug prefix. When both markets fall inside a league and either one names an entity, they must share an entity. Kalshi game-level markets also get `sharedTeamBonus` per shared entity. `python3 scripts/build_entities.py` rebuilds the file. The NBA, NHL, NFL and MLB are checked code by code against Polymarket's list. Other leagues are linked by pairing Kalshi and Polymarket games on the same date. Re-running it adds new teams and keeps the checked ones.
   - **Ranking.** Each Kalshi market keeps its `candidatesPerMarket` best Polymarket matches, ranked by score and then by closeness of event time. Each Polymarket market keeps its `candidatesPerPolymarketMarket` best Kalshi matches, so hourly ladders like Bitcoin don't flood the LLM.
   - **LLM.** Cached verdicts are applied first. Unknown pairs then go to the LLM with `concurrentRequests` requests in flight, paced to `requestsPerMinute`.
3. **Arbitrage engine** (`src/arbitrage`) holds four ask ladders per pair: Kalshi YES, Kalshi NO, Polymarket YES-equivalent and Polymarket NO-equivalent. After each update it re-evaluates the one trade direction that update affects:
   `edge = $1 - kalshiAsk - polymarketAsk - kalshiFee - polymarketFee`
4. **Binary logging** (`src/logging`) writes fixed 64-byte records. A background thread drains them to `data/logs/*.bin`. Decode them with `kparb_logdecode`.

### Discovery cycles

- Only markets whose event falls within `discovery.maximumDaysUntilResolution` are considered (default 2 days). For Kalshi this is `expected_expiration_time`. For Polymarket it is `gameStartTime` when present, otherwise `endDate`. Polymarket's `endDate` is a resolution deadline, and game moneylines set it a week after the game. A second Gamma fetch with `sports_market_types=moneyline`, reaching `polymarketMoneylineSearchDays` ahead, picks those up.
- Markets must also be open for trading at discovery time. On Kalshi that means `status` is `active` and `open_time` has passed. On Polymarket it means `acceptingOrders` is true and `acceptingOrdersTimestamp` (or `startDate`) has passed.
- Matching runs on a background thread. Streaming starts as soon as either `pairsBeforeStreaming` pairs (default 500) are matched, `maximumMinutesBeforeStreaming` (default 5) have passed, or matching finishes.
- Matching keeps running while the stream is live. The stream restarts with the larger pair set each time another `pairsBetweenStreamRestarts` pairs (default 25) are matched, and once more when matching finishes.
- Discovery and matching rerun every `rematchIntervalMinutes` (default 60) so new short-dated markets get picked up. Cached verdicts make later cycles cheap.
- After `maximumConsecutiveFailures` LLM errors in a row, for example an exhausted rate-limit quota, the LLM is skipped until the next cycle.

### Sizing

For each crossed pair, the engine walks both ask ladders together, taking `min(remaining Kalshi quantity, remaining Polymarket quantity)` at each step. It stops when the marginal edge drops below `minimumEdgeTicks`, a book runs out, or `maximumDepthLevels` is reached. The reported size is the largest quantity at which every contract still clears the minimum edge. It does not apply balance, position or minimum-order-size limits.

### Hot path guarantees

- The feeds hand updates to the engine through single-producer/single-consumer rings. Pushes and pops are wait-free: there are no locks or compare-and-swap loops, and a full ring never blocks a producer.
- Prices are integer ticks of $0.001. Each ladder is a price-indexed array plus a 1024-bit occupancy bitset, so updates are O(1) and the best ask is found by scanning at most 16 words with `countr_zero`.
- After startup there are no heap allocations. `HotPathAllocationTest`, `KalshiMessageParserTest` and `PolymarketMessageParserTest` check this by counting `operator new` calls.
- The engine processes updates in batches and prefetches the ladder, bitset word, price level and pair quote that the update 4 positions ahead will touch.
- A full feed ring triggers an in-order venue reset followed by a reconnect. The engine never trades on a book that silently lost an update.

## Build

Requires CMake 3.24+, Ninja, a C++20 compiler and OpenSSL 3 (`brew install cmake ninja openssl@3`). The other dependencies are downloaded by FetchContent.

```sh
cmake --preset release && cmake --build --preset release
ctest --preset release
./build/release/benchmarks/kparb_engine_benchmark
```

The `debug`, `asan` and `tsan` presets also exist.

### Benchmark (Apple M2, release build)

The benchmark compares a quiet market, where pair costs are around $1.06 and arbitrage is rare, with a stressed market, where costs sit around $1.00 so pairs are constantly crossed and every update re-runs the depth walk.

| Scenario | Pairs | Prefetch off | Prefetch on |
|---|---|---|---|
| Quiet, direct apply | 2,000 | 11.9 ns/update | 11.9 ns/update |
| Quiet, through SPSC queue | 2,000 | 12.7 ns/update | 13.3 ns/update |
| Quiet, direct apply | 20,000 | 26.8 ns/update | 19.0 ns/update |
| Quiet, through SPSC queue | 20,000 | 30.5 ns/update | 19.9 ns/update |
| Stressed, direct apply | 2,000 | 224 ns/update | 221 ns/update |

At 2,000 pairs the data the engine touches fits in L2, so prefetching has nothing to hide. Once the working set outgrows the cache, prefetching makes updates 1.4–1.5× faster. Run it with `kparb_engine_benchmark [pairCount]`.

## Configure

Secrets live in `.env`, which is gitignored. See `.env.example`:

```
KALSHI_API_KEY_ID=...
KALSHI_PRIVATE_KEY="-----BEGIN PRIVATE KEY-----\n...\n-----END PRIVATE KEY-----\n"
KPARB_LLM_API_KEY=...
```

`KALSHI_PRIVATE_KEY_PATH` can be used instead of an inline key. Optional settings go in `config/config.json`: copy `config/config.example.json` and edit it. Every field is optional.

To force a pair in or out of the matched set, create `data/manual_matches.json`:

```json
{
  "include": [{"kalshiTicker": "KX...", "polymarketIdentifier": "0x<conditionId>", "isInverted": false}],
  "exclude": [{"kalshiTicker": "KX...", "polymarketIdentifier": "0x<conditionId>"}]
}
```

## Run

```sh
./build/release/src/app/kparb --match-only
./build/release/src/app/kparb --candidates-only
./build/release/src/app/kparb --use-saved-pairs
./build/release/src/app/kparb
./build/release/src/app/kparb --use-saved-pairs --duration-seconds 600
```

- `--match-only` discovers markets, matches them, writes `data/matched_pairs.json`, then exits.
- `python3 scripts/snapshot_edges.py` reads `data/matched_pairs.json`, pulls the current order books from both venues and prints the all-in cost of the cheaper hedge for each pair after taker fees.
- `--candidates-only` discovers markets and writes candidate pairs to `data/candidates.tsv`, along with any cached verdict. It makes no LLM calls, so it is a free way to tune the matching settings.
- `--use-saved-pairs` streams the pairs saved by the last match.
- With no flags, it discovers, matches and streams in one run.
- `--duration-seconds` stops streaming after the given number of seconds.

## Decode logs

```sh
./build/release/src/tools/kparb_logdecode data/logs/kparb-<timestamp>.bin
./build/release/src/tools/kparb_logdecode data/logs/kparb-<timestamp>.bin --type opportunity --csv > opportunities.csv
```

The available record types are `opportunity`, `opportunity_closed`, `feed_event`, `engine_stats`, `match_event` and `pair_name`.
