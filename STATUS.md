# Status

## Verified working (live, 2026-11-03)

- Kalshi REST discovery. Uses cursor pagination, `mve_filter=exclude`, and a `min_close_ts`/`max_close_ts` window, then filters on `expected_expiration_time`.
- Polymarket Gamma discovery. Uses `/markets/keyset` with `after_cursor` and an `end_date_min`/`end_date_max` window. Per-market taker fees come from `feeSchedule`.
- Polymarket market WebSocket. It connects, subscribes, receives book snapshots and price changes, and sends text pings.
- Candidate generation. The 2-day window produced 5,524 candidate pairs.

## Credentials

- The Kalshi production key works. It is an Ed25519 key, which Kalshi accepts, and the signer picks the scheme from the key. Verified on 2026-11-03: authenticated balance request, plus a live WebSocket stream with subscriptions confirmed and snapshots and deltas applied.
- Groq works with `openai/gpt-oss-120b`. Groq's JSON mode produces malformed output with this model, so it is turned off; the verdict parser pulls the JSON object out of plain text instead. `reasoning_effort` is set to `low`.
- Groq free-tier limits seen: 1,000 requests a day and 8,000 tokens a minute. A verification uses about 420 tokens, so the client is paced at 10 requests a minute.

## Things to know

- With the 2-day window and Groq's free tier, the daily token limit is the bottleneck, not requests per minute. Verdicts are cached, so each cycle continues where the last one stopped. Check your limits at console.groq.com.
- Streaming starts once 500 pairs are matched or 5 minutes have passed, whichever comes first. After that the stream restarts each time another 25 pairs are matched, and once when matching finishes. The time cap exists because only about 5% of candidates are confirmed: the first live run accepted 8 of 170, and at roughly 10 checks a minute on the free tier, 500 pairs is out of reach in a single day. Discovery and matching rerun every 60 minutes, so new short-dated markets get picked up.
- Kalshi fees are approximated per contract as `ceil(0.07 × p × (1 − p))` in $0.001 ticks. Kalshi actually rounds the fee up to the cent on the whole order, so on large orders this slightly overstates fees.
- Sizing walks both order books until the marginal edge drops below `minimumEdgeTicks`. There are no balance, position or notional caps, and Polymarket's `orderMinSize` is not enforced yet.
- Polymarket subscriptions are sent in chunks of 500 tokens on one connection. If Polymarket limits assets per connection, the feed would need to be sharded across connections.
- The project lives in `~/Desktop`, which iCloud syncs. With Optimize Mac Storage turned on, macOS evicted 2,210 build files and corrupted the build: the compiler read evicted headers as empty. Builds now go to `build.nosync/`, which iCloud skips. iCloud also syncs `.env` (private keys), `data/` and `.git`, and can evict them under disk pressure. Moving the repository out of `~/Desktop` (for example to `~/Projects/KPArb`) avoids all of this.
- A market only counts as open when Kalshi reports `status` `active` with `open_time` in the past, or Polymarket reports `acceptingOrders` with `acceptingOrdersTimestamp` (or `startDate`) in the past. On 2026-11-04 this dropped 54 of 29,875 Polymarket markets.
