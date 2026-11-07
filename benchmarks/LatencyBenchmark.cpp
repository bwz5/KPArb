#include "arbitrage/ArbitrageEngine.h"
#include "common/Clock.h"
#include "ingestion/KalshiMessageParser.h"
#include "ingestion/LadderDirectory.h"
#include "ingestion/PolymarketMessageParser.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace kparb;

std::uint32_t pairCount = 500;
constexpr std::size_t snapshotLevelsPerSide = 25;
constexpr std::size_t bulkIterations = 2'000'000;
constexpr std::size_t sampledIterations = 200'000;

template <typename Value>
inline void keepAlive(const Value& value) noexcept
{
    asm volatile("" : : "r,m"(value) : "memory");
}

struct Percentiles {
    double p50;
    double p90;
    double p99;
    double p999;
    double maximum;
};

Percentiles percentilesOf(std::vector<std::int64_t>& samples, double subtract)
{
    std::sort(samples.begin(), samples.end());
    auto at = [&](double fraction) {
        const auto index = std::min(samples.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(samples.size())));
        return std::max(0.0, static_cast<double>(samples[index]) - subtract);
    };
    return Percentiles{at(0.50), at(0.90), at(0.99), at(0.999), std::max(0.0, static_cast<double>(samples.back()) - subtract)};
}

double timerOverheadNanos()
{
    std::vector<std::int64_t> samples;
    samples.reserve(sampledIterations);
    for (std::size_t index = 0; index < sampledIterations; ++index) {
        const std::int64_t start = steadyNanoseconds();
        samples.push_back(steadyNanoseconds() - start);
    }
    std::sort(samples.begin(), samples.end());
    return static_cast<double>(samples[samples.size() / 2]);
}

void printRow(const std::string& name, double meanNanos)
{
    std::cout << "  " << std::left << std::setw(58) << name << std::right << std::setw(9) << meanNanos << " ns mean\n";
}

void printRow(const std::string& name, double meanNanos, const Percentiles& percentiles)
{
    std::cout << "  " << std::left << std::setw(58) << name << std::right << std::setw(9) << meanNanos << " ns mean"
              << "   p50 " << std::setw(7) << percentiles.p50 << "   p90 " << std::setw(7) << percentiles.p90 << "   p99 "
              << std::setw(7) << percentiles.p99 << "   p99.9 " << std::setw(8) << percentiles.p999 << "\n";
}

template <typename Operation>
double bulkMeanNanos(std::size_t iterations, Operation&& operation)
{
    double best = 1e18;
    for (int repetition = 0; repetition < 5; ++repetition) {
        const std::int64_t start = steadyNanoseconds();
        for (std::size_t index = 0; index < iterations; ++index) {
            operation(index);
        }
        best = std::min(best, static_cast<double>(steadyNanoseconds() - start) / static_cast<double>(iterations));
    }
    return best;
}

template <typename Operation>
Percentiles sampledPercentiles(std::size_t iterations, double overhead, Operation&& operation)
{
    std::vector<std::int64_t> samples;
    samples.reserve(iterations);
    for (std::size_t index = 0; index < iterations; ++index) {
        const std::int64_t start = steadyNanoseconds();
        operation(index);
        samples.push_back(steadyNanoseconds() - start);
    }
    return percentilesOf(samples, overhead);
}

struct EngineHarness {
    std::unique_ptr<LogChannel> channel = std::make_unique<LogChannel>(1);
    std::unique_ptr<ArbitrageEngine> engine;
    std::atomic<bool> stopDraining{false};
    std::thread drainer;

    EngineHarness()
    {
        EngineSettings settings;
        settings.minimumEdgeTicks = 5;
        engine = std::make_unique<ArbitrageEngine>(pairCount, FeeSchedule::quadratic(0.07), FeeSchedule::zero(), settings, *channel);
        for (std::uint32_t ladderIndex = 0; ladderIndex < pairCount * laddersPerPair; ++ladderIndex) {
            engine->applyUpdate(LadderUpdate{0, ladderIndex, 0, 0, UpdateKind::ClearLadder, Venue::Kalshi});
        }
        drainer = std::thread([this] {
            LogRecord record{};
            while (!stopDraining.load(std::memory_order_relaxed)) {
                while (channel->records().tryPop(record)) {
                }
                std::this_thread::yield();
            }
        });
    }

    ~EngineHarness()
    {
        stopDraining.store(true);
        drainer.join();
    }

    void seedBooks(Ticks kalshiAsk, Ticks polymarketAsk)
    {
        for (std::uint32_t pairIndex = 0; pairIndex < pairCount; ++pairIndex) {
            for (std::uint32_t slot = 0; slot < laddersPerPair; ++slot) {
                const Ticks base = slot < 2 ? kalshiAsk : polymarketAsk;
                const Venue venue = slot < 2 ? Venue::Kalshi : Venue::Polymarket;
                for (Ticks level = 0; level < 10; ++level) {
                    engine->applyUpdate(LadderUpdate{1, pairIndex * laddersPerPair + slot, 1000, static_cast<Ticks>(base + level * 10),
                        UpdateKind::SetLevel, venue});
                }
            }
        }
    }
};

std::vector<LadderUpdate> randomLevelChanges(std::size_t count, Ticks center, std::uint32_t seed)
{
    std::mt19937_64 generator(seed);
    std::uniform_int_distribution<std::uint32_t> ladderDistribution(0, pairCount * laddersPerPair - 1);
    std::uniform_int_distribution<int> offsetDistribution(5, 95);
    std::uniform_int_distribution<std::int32_t> quantityDistribution(1, 5000);
    std::bernoulli_distribution removalDistribution(0.25);
    std::vector<LadderUpdate> updates;
    updates.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint32_t ladderIndex = ladderDistribution(generator);
        const auto price = static_cast<Ticks>(center + offsetDistribution(generator));
        const std::int32_t quantity = removalDistribution(generator) ? 0 : quantityDistribution(generator);
        const Venue venue = slotOfLadder(ladderIndex) < 2 ? Venue::Kalshi : Venue::Polymarket;
        updates.push_back(LadderUpdate{1, ladderIndex, quantity, price, UpdateKind::SetLevel, venue});
    }
    return updates;
}

void measureBookReconstruction()
{
    std::cout << "order book reconstruction (one AskLadder, no arb check)\n";
    std::vector<AskLadder> ladders(static_cast<std::size_t>(pairCount) * laddersPerPair);
    for (auto& ladder : ladders) {
        ladder.clear();
    }
    const auto changes = randomLevelChanges(bulkIterations, 400, 7);
    const double levelChange = bulkMeanNanos(changes.size(), [&](std::size_t index) {
        const LadderUpdate& update = changes[index];
        ladders[update.ladderIndex].setLevel(update.price, static_cast<Quantity>(update.quantity));
        keepAlive(ladders[update.ladderIndex].bestPrice());
    });
    printRow("apply one price-level change, read best ask", levelChange);

    std::mt19937_64 generator(11);
    std::uniform_int_distribution<int> priceDistribution(1, 999);
    std::vector<std::array<Ticks, snapshotLevelsPerSide>> snapshots(4096);
    for (auto& snapshot : snapshots) {
        for (auto& price : snapshot) {
            price = static_cast<Ticks>(priceDistribution(generator));
        }
    }
    auto rebuild = [&](std::size_t index) {
        AskLadder& ladder = ladders[index % ladders.size()];
        ladder.clear();
        for (const Ticks price : snapshots[index % snapshots.size()]) {
            ladder.setLevel(price, 100);
        }
        keepAlive(ladder.bestPrice());
    };
    const double snapshotRebuild = bulkMeanNanos(bulkIterations / 10, rebuild);
    printRow("rebuild a ladder from a " + std::to_string(snapshotLevelsPerSide) + "-level snapshot", snapshotRebuild);
    std::cout << '\n';
}

void measureArbitrageCheck(double overhead)
{
    std::cout << "engine.applyUpdate: ladder update + top-of-book refresh + arb check, " << pairCount << " pairs\n";
    {
        EngineHarness harness;
        harness.seedBooks(530, 530);
        const auto changes = randomLevelChanges(bulkIterations, 530, 13);
        const double mean = bulkMeanNanos(changes.size(), [&](std::size_t index) { harness.engine->applyUpdate(changes[index]); });
        const auto percentiles = sampledPercentiles(sampledIterations, overhead,
            [&](std::size_t index) { harness.engine->applyUpdate(changes[index]); });
        printRow("no arb on the pair (cost ~$1.06, common case)", mean, percentiles);
        keepAlive(harness.engine->counters().opportunitiesOpened);
    }
    {
        EngineHarness harness;
        harness.seedBooks(400, 400);
        const auto changes = randomLevelChanges(bulkIterations, 400, 17);
        const double mean = bulkMeanNanos(changes.size(), [&](std::size_t index) { harness.engine->applyUpdate(changes[index]); });
        const auto percentiles = sampledPercentiles(sampledIterations, overhead,
            [&](std::size_t index) { harness.engine->applyUpdate(changes[index]); });
        printRow("arb open on the pair (cost ~$0.80, depth walk + log record)", mean, percentiles);
        std::cout << "    opportunity records emitted: " << harness.engine->counters().opportunitiesOpened
                  << " opened, dropped log records: " << harness.channel->droppedRecordCount() << '\n';
    }
    std::cout << '\n';
}

std::string kalshiTicker(std::uint32_t pairIndex)
{
    return "KXNCAAMBGAME-26NOV05TEAM" + std::to_string(pairIndex) + "-AWAY";
}

std::string polymarketToken(std::uint32_t pairIndex, bool isYes)
{
    std::string token = std::to_string(pairIndex * 2 + (isYes ? 0 : 1));
    return std::string(77 - token.size(), '7') + token;
}

std::string dollars(Ticks price)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(4) << static_cast<double>(price) / 1000.0;
    return stream.str();
}

std::string kalshiDelta(std::uint32_t pairIndex, Ticks bid, int delta, std::uint64_t sequence)
{
    return R"({"type":"orderbook_delta","sid":1,"seq":)" + std::to_string(sequence) + R"(,"msg":{"market_ticker":")"
        + kalshiTicker(pairIndex) + R"(","market_id":"9d2b1c4e-1111-4b2a-9c55-1234567890ab","price_dollars":")" + dollars(bid)
        + R"(","delta_fp":")" + std::to_string(delta) + R"(.00","side":")" + (sequence % 2 ? "yes" : "no")
        + R"(","ts":"2026-11-05T18:00:00.000000Z"}})";
}

std::string kalshiSnapshot(std::uint32_t pairIndex)
{
    std::string yes;
    std::string no;
    for (std::size_t level = 0; level < snapshotLevelsPerSide; ++level) {
        const auto price = static_cast<Ticks>(10 + level * 10);
        yes += (level ? "," : "") + std::string(R"([")") + dollars(price) + R"(",")" + std::to_string(100 + level) + R"(.00"])";
        no += (level ? "," : "") + std::string(R"([")") + dollars(price) + R"(",")" + std::to_string(200 + level) + R"(.00"])";
    }
    return R"({"type":"orderbook_snapshot","sid":1,"seq":1,"msg":{"market_ticker":")" + kalshiTicker(pairIndex)
        + R"(","market_id":"9d2b1c4e-1111-4b2a-9c55-1234567890ab","yes_dollars_fp":[)" + yes + R"(],"no_dollars_fp":[)" + no + "]}}";
}

std::string polymarketPriceChange(std::uint32_t pairIndex, Ticks price, int size)
{
    return R"({"market":"0x5f65177b394277fd294cd75650044e32ba009a95022d88a0c1d565897d72f8f1","price_changes":[{"asset_id":")"
        + polymarketToken(pairIndex, true) + R"(","price":")" + dollars(price) + R"(","size":")" + std::to_string(size)
        + R"(","side":"SELL","hash":"56621a121a47ed9333273e21c83b660cff37ae50","best_bid":"0.49","best_ask":"0.51"}],)"
        + R"("timestamp":"1793900000000","event_type":"price_change"})";
}

std::string polymarketBook(std::uint32_t pairIndex)
{
    std::string bids;
    std::string asks;
    for (std::size_t level = 0; level < snapshotLevelsPerSide; ++level) {
        bids += (level ? "," : "") + std::string(R"({"price":")") + dollars(static_cast<Ticks>(480 - level * 10)) + R"(","size":")"
            + std::to_string(100 + level) + R"("})";
        asks += (level ? "," : "") + std::string(R"({"price":")") + dollars(static_cast<Ticks>(520 + level * 10)) + R"(","size":")"
            + std::to_string(100 + level) + R"("})";
    }
    return R"({"market":"0x5f65177b394277fd294cd75650044e32ba009a95022d88a0c1d565897d72f8f1","asset_id":")"
        + polymarketToken(pairIndex, true) + R"(","timestamp":"1793900000000","hash":"0xabc","bids":[)" + bids + R"(],"asks":[)" + asks
        + R"(],"tick_size":"0.01","event_type":"book"})";
}

struct UpdateBuffer {
    std::array<LadderUpdate, 256> updates{};
    std::size_t count = 0;

    void operator()(const LadderUpdate& update) noexcept
    {
        if (count < updates.size()) {
            updates[count++] = update;
        }
    }
};

template <typename Parser>
void measureMessages(const char* label, Parser& parser, ArbitrageEngine& engine, const std::vector<std::string>& messages, double overhead)
{
    UpdateBuffer buffer;
    LadderUpdateSink sink(buffer);
    const double parseMean = bulkMeanNanos(bulkIterations / 4, [&](std::size_t index) {
        buffer.count = 0;
        parser.parse(messages[index % messages.size()], 1, sink);
        keepAlive(buffer.count);
    });
    auto endToEnd = [&](std::size_t index) {
        buffer.count = 0;
        parser.parse(messages[index % messages.size()], steadyNanoseconds(), sink);
        engine.applyBatch<true>(buffer.updates.data(), buffer.count);
    };
    const double endToEndMean = bulkMeanNanos(bulkIterations / 4, endToEnd);
    const auto endToEndPercentiles = sampledPercentiles(sampledIterations, overhead, endToEnd);
    buffer.count = 0;
    parser.parse(messages.front(), 1, sink);
    const auto bytes = messages.front().size();
    std::cout << "  " << label << " (" << bytes << " bytes, " << buffer.count << " ladder updates)\n";
    printRow("    parse only (simdjson -> LadderUpdates)", parseMean);
    printRow("    parse + rebuild book + arb check", endToEndMean, endToEndPercentiles);
}

void measureMessagePipeline(double overhead)
{
    std::cout << "single message, websocket text -> arb decision on the feed thread's data\n";
    std::vector<PairSubscription> subscriptions;
    for (std::uint32_t pairIndex = 0; pairIndex < pairCount; ++pairIndex) {
        subscriptions.push_back(PairSubscription{kalshiTicker(pairIndex), polymarketToken(pairIndex, true), polymarketToken(pairIndex, false)});
    }
    const LadderDirectory directory(subscriptions);
    KalshiMessageParser kalshiParser(directory);
    PolymarketMessageParser polymarketParser(directory);
    EngineHarness harness;
    harness.seedBooks(530, 530);

    std::mt19937_64 generator(23);
    std::uniform_int_distribution<std::uint32_t> pairDistribution(0, pairCount - 1);
    std::uniform_int_distribution<int> priceDistribution(400, 600);
    std::uniform_int_distribution<int> deltaDistribution(-50, 50);
    std::vector<std::string> kalshiDeltas;
    std::vector<std::string> polymarketChanges;
    std::vector<std::string> kalshiSnapshots;
    std::vector<std::string> polymarketBooks;
    for (std::uint64_t index = 0; index < 4096; ++index) {
        const std::uint32_t pairIndex = pairDistribution(generator);
        kalshiDeltas.push_back(kalshiDelta(pairIndex, static_cast<Ticks>(priceDistribution(generator)), deltaDistribution(generator), index));
        polymarketChanges.push_back(polymarketPriceChange(pairIndex, static_cast<Ticks>(priceDistribution(generator)), deltaDistribution(generator) + 50));
    }
    for (std::uint32_t index = 0; index < 512; ++index) {
        kalshiSnapshots.push_back(kalshiSnapshot(pairDistribution(generator)));
        polymarketBooks.push_back(polymarketBook(pairDistribution(generator)));
    }
    measureMessages("Kalshi orderbook_delta", kalshiParser, *harness.engine, kalshiDeltas, overhead);
    measureMessages("Polymarket price_change", polymarketParser, *harness.engine, polymarketChanges, overhead);
    measureMessages("Kalshi orderbook_snapshot", kalshiParser, *harness.engine, kalshiSnapshots, overhead);
    measureMessages("Polymarket book", polymarketParser, *harness.engine, polymarketBooks, overhead);
    std::cout << '\n';
}

void measureQueueHop(double overhead)
{
    std::cout << "feed thread -> engine thread hop through the SPSC ring (one update at a time)\n";
    auto queue = std::make_unique<LadderUpdateQueue>();
    constexpr std::size_t hopCount = 200'000;
    std::vector<std::int64_t> samples;
    samples.reserve(hopCount);
    std::atomic<bool> consumerReady{false};
    std::thread consumer([&] {
        consumerReady.store(true, std::memory_order_release);
        LadderUpdate update{};
        while (samples.size() < hopCount) {
            if (queue->tryPop(update)) {
                samples.push_back(steadyNanoseconds() - update.receivedAtNanos);
            }
        }
    });
    while (!consumerReady.load(std::memory_order_acquire)) {
    }
    for (std::size_t index = 0; index < hopCount; ++index) {
        const std::int64_t sentAt = steadyNanoseconds();
        while (!queue->tryPush(LadderUpdate{sentAt, 0, 1, 500, UpdateKind::SetLevel, Venue::Kalshi})) {
        }
        const std::int64_t waitUntil = sentAt + 2000;
        while (steadyNanoseconds() < waitUntil) {
        }
    }
    consumer.join();
    const auto percentiles = percentilesOf(samples, overhead / 2);
    std::cout << "  one-way handoff latency" << std::string(35, ' ') << "          p50 " << std::setw(7) << percentiles.p50
              << "   p90 " << std::setw(7) << percentiles.p90 << "   p99 " << std::setw(7) << percentiles.p99 << "   p99.9 "
              << std::setw(8) << percentiles.p999 << "\n\n";
}

}

int main(int argumentCount, char** arguments)
{
    if (argumentCount > 1) {
        pairCount = static_cast<std::uint32_t>(std::strtoul(arguments[1], nullptr, 10));
    }
    std::cout << std::fixed << std::setprecision(1);
    const double overhead = timerOverheadNanos();
    std::cout << "pairs=" << pairCount << " ladder_bytes=" << sizeof(AskLadder) << " timer_overhead_ns=" << overhead
              << " (subtracted from percentiles)\n\n";
    measureBookReconstruction();
    measureArbitrageCheck(overhead);
    measureMessagePipeline(overhead);
    measureQueueHop(overhead);
    return 0;
}
