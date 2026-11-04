#include "arbitrage/ArbitrageEngine.h"
#include "common/Clock.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <thread>
#include <vector>

namespace {

using namespace kparb;

constexpr std::uint32_t pairCount = 2000;
constexpr std::size_t updateCount = 4'000'000;
constexpr int repetitionCount = 5;

std::vector<LadderUpdate> generateUpdates()
{
    std::mt19937_64 generator(42);
    std::uniform_int_distribution<std::uint32_t> ladderDistribution(0, pairCount * laddersPerPair - 1);
    std::normal_distribution<double> priceDistribution(500.0, 60.0);
    std::uniform_int_distribution<std::int32_t> quantityDistribution(0, 5000);
    std::bernoulli_distribution removalDistribution(0.25);

    std::vector<LadderUpdate> updates;
    updates.reserve(updateCount);
    for (std::size_t index = 0; index < updateCount; ++index) {
        const std::uint32_t ladderIndex = ladderDistribution(generator);
        const double rawPrice = std::clamp(priceDistribution(generator), 1.0, 999.0);
        const auto price = static_cast<Ticks>(rawPrice);
        const std::int32_t quantity = removalDistribution(generator) ? 0 : quantityDistribution(generator);
        const Venue venue = slotOfLadder(ladderIndex) < 2 ? Venue::Kalshi : Venue::Polymarket;
        updates.push_back(LadderUpdate{static_cast<std::int64_t>(index), ladderIndex, quantity, price, UpdateKind::SetLevel, venue});
    }
    return updates;
}

struct BenchmarkContext {
    std::unique_ptr<LogChannel> channel = std::make_unique<LogChannel>(1);
    std::unique_ptr<ArbitrageEngine> engine;
    std::atomic<bool> stopDraining{false};
    std::thread drainer;

    BenchmarkContext()
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

    ~BenchmarkContext()
    {
        stopDraining.store(true);
        drainer.join();
    }
};

template <bool usePrefetch>
double measureDirectApply(const std::vector<LadderUpdate>& updates)
{
    BenchmarkContext context;
    double best = 1e18;
    for (int repetition = 0; repetition < repetitionCount; ++repetition) {
        const std::int64_t start = steadyNanoseconds();
        for (std::size_t offset = 0; offset < updates.size(); offset += ArbitrageEngine::updateBatchSize) {
            const std::size_t count = std::min(ArbitrageEngine::updateBatchSize, updates.size() - offset);
            context.engine->applyBatch<usePrefetch>(updates.data() + offset, count);
        }
        const std::int64_t elapsed = steadyNanoseconds() - start;
        best = std::min(best, static_cast<double>(elapsed) / static_cast<double>(updates.size()));
    }
    std::cout << "  opportunities opened: " << context.engine->counters().opportunitiesOpened
              << ", dropped log records: " << context.channel->droppedRecordCount() << '\n';
    return best;
}

template <bool usePrefetch>
double measureThroughQueue(const std::vector<LadderUpdate>& updates)
{
    BenchmarkContext context;
    auto queue = std::make_unique<LadderUpdateQueue>();
    double best = 1e18;
    for (int repetition = 0; repetition < repetitionCount; ++repetition) {
        std::atomic<bool> producerFinished{false};
        const std::int64_t start = steadyNanoseconds();
        std::thread producer([&] {
            for (const auto& update : updates) {
                while (!queue->tryPush(update)) {
                }
            }
            producerFinished.store(true, std::memory_order_release);
        });
        std::size_t consumed = 0;
        while (consumed < updates.size()) {
            consumed += context.engine->drainQueue<usePrefetch>(*queue);
        }
        producer.join();
        const std::int64_t elapsed = steadyNanoseconds() - start;
        best = std::min(best, static_cast<double>(elapsed) / static_cast<double>(updates.size()));
    }
    return best;
}

}

int main()
{
    const auto updates = generateUpdates();
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "pairs=" << pairCount << " ladders=" << pairCount * laddersPerPair << " updates=" << updates.size()
              << " ladder_bytes=" << sizeof(AskLadder) << "\n\n";

    std::cout << "direct apply, prefetch off\n";
    const double directWithout = measureDirectApply<false>(updates);
    std::cout << "  " << directWithout << " ns/update\n";
    std::cout << "direct apply, prefetch on\n";
    const double directWith = measureDirectApply<true>(updates);
    std::cout << "  " << directWith << " ns/update\n";

    std::cout << "through spsc queue, prefetch off\n";
    const double queueWithout = measureThroughQueue<false>(updates);
    std::cout << "  " << queueWithout << " ns/update\n";
    std::cout << "through spsc queue, prefetch on\n";
    const double queueWith = measureThroughQueue<true>(updates);
    std::cout << "  " << queueWith << " ns/update\n";

    std::cout << "\nprefetch speedup direct: " << directWithout / directWith << "x, queue: " << queueWithout / queueWith << "x\n";
    return 0;
}
