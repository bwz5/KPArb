# KPArb

Detects arbitrage between equivalent Kalshi and Polymarket binary markets. It only detects and logs opportunities; it never places orders.

## How it works

1. **Ingestion** (`src/ingestion`) downloads every open binary market from the Kalshi REST API and the Polymarket Gamma API. It then streams order books over the Kalshi WebSocket (signed with your API key) and the Polymarket market WebSocket.
2. **Matching** (`src/matching`) finds candidate pairs with a TF-IDF index. A candidate must have a close date within a window of the other market's, and numeric thresholds that agree. An open-weights LLM behind an OpenAI-compatible API (Groq by default) then confirms that each pair settles on the same outcome, and which way round the outcomes line up. Every verdict is cached in `data/match_cache.json`.
3. **Arbitrage engine** (`src/arbitrage`) holds four ask ladders per pair: Kalshi YES, Kalshi NO, Polymarket YES-equivalent and Polymarket NO-equivalent. After each update it re-evaluates the one trade direction that update affects:
   `edge = $1 - kalshiAsk - polymarketAsk - kalshiFee - polymarketFee`
4. **Binary logging** (`src/logging`) writes fixed 64-byte records. A background thread drains them to `data/logs/*.bin`. Decode them with `kparb_logdecode`.

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
./build/release/src/app/kparb --use-saved-pairs
./build/release/src/app/kparb
./build/release/src/app/kparb --use-saved-pairs --duration-seconds 600
```

- `--match-only` discovers markets, matches them, writes `data/matched_pairs.json`, then exits.
- `--use-saved-pairs` streams the pairs saved by the last match.
- With no flags, it discovers, matches and streams in one run.
- `--duration-seconds` stops streaming after the given number of seconds.

## Decode logs

```sh
./build/release/src/tools/kparb_logdecode data/logs/kparb-<timestamp>.bin
./build/release/src/tools/kparb_logdecode data/logs/kparb-<timestamp>.bin --type opportunity --csv > opportunities.csv
```

The available record types are `opportunity`, `opportunity_closed`, `feed_event`, `engine_stats`, `match_event` and `pair_name`.
