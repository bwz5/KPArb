# Status

## Verified working (live, 2026-11-03)

- Kalshi REST discovery. Uses cursor pagination, `mve_filter=exclude`, and a `min_close_ts`/`max_close_ts` window, then filters on `expected_expiration_time`.
- Polymarket Gamma discovery. Uses `/markets/keyset` with `after_cursor` and an `end_date_min`/`end_date_max` window. Per-market taker fees come from `feeSchedule`.
- Polymarket market WebSocket. It connects, subscribes, receives book snapshots and price changes, and sends text pings.
- Candidate generation. The 2-day window produced 5,524 candidate pairs on 2026-11-03 with title-only TF-IDF. On 2026-11-04, structural filters (market type, strike, team codes, subject team) replaced it.
- The overnight run on 2026-11-03/04 confirmed only 11 pairs, for two reasons. First, the free Groq tier allowed about 10 checks an hour. Second, about 93% of LLM checks were structurally hopeless: game winners against spreads, NHL overtime against college football, Bitcoin midnight against noon. Polymarket moneylines were missing entirely, because their `endDate` falls a week after the game. The live candidate set changed from about 7,500 mostly-wrong pairs to about 4,900 pairs. Most of these are line-for-line spreads and totals, plus game winners matched to moneylines.

## Credentials

- The Kalshi production key works. It is an Ed25519 key, which Kalshi accepts, and the signer picks the scheme from the key. Verified on 2026-11-03: authenticated balance request, plus a live WebSocket stream with subscriptions confirmed and snapshots and deltas applied.
- Groq works with `openai/gpt-oss-120b`. Groq's JSON mode produces malformed output with this model, so it is turned off; the verdict parser pulls the JSON object out of plain text instead. `reasoning_effort` is set to `low`.
- Groq free-tier limits seen: 1,000 requests a day, 8,000 tokens a minute and 200,000 tokens a day. The defaults now assume a paid tier: 250 requests a minute and 8 concurrent requests. On the free tier, set `languageModel.requestsPerMinute` to 10 and `matching.concurrentRequests` to 1. The matcher waits out quota errors of up to 15 minutes either way.

## Things to know

- Streaming starts once 500 pairs are matched or 5 minutes have passed, whichever comes first. After that the stream restarts each time another 25 pairs are matched, and once when matching finishes. Discovery and matching rerun every 60 minutes, so new short-dated markets get picked up.
- Kalshi publishes some hourly markets, such as the noon Bitcoin ladder, with `status: initialized` until about an hour before they settle. The open-market filter skips them until then.
- The end-of-session summary reports the peak number of synchronized ladders, because shutting the feeds down resets the live count to zero.
- Kalshi fees are approximated per contract as `ceil(0.07 × p × (1 − p))` in $0.001 ticks. Kalshi actually rounds the fee up to the cent on the whole order, so on large orders this slightly overstates fees.
- Sizing walks both order books until the marginal edge drops below `minimumEdgeTicks`. There are no balance, position or notional caps, and Polymarket's `orderMinSize` is not enforced yet.
- Polymarket subscriptions are sent in chunks of 500 tokens on one connection. If Polymarket limits assets per connection, the feed would need to be sharded across connections.
- The repository moved from `~/Desktop/Workspace/Projects/KPArb` to `~/Projects/KPArb` on 2026-11-04. The Desktop is synced by iCloud, which offloaded build files and broke compilation; it also uploaded `.env`. `build.nosync/` stays in `.gitignore` for anyone building from an iCloud folder.
- A market only counts as open when Kalshi reports `status` `active` with `open_time` in the past, or Polymarket reports `acceptingOrders` with `acceptingOrdersTimestamp` (or `startDate`) in the past. On 2026-11-04 this dropped 54 of 29,875 Polymarket markets.
