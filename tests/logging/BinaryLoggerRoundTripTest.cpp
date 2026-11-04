#include "logging/BinaryLogReader.h"
#include "logging/BinaryLogger.h"
#include "logging/RecordTypes.h"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <thread>
#include <vector>

namespace kparb {
namespace {

std::filesystem::path temporaryLogPath(const std::string& name)
{
    return std::filesystem::temp_directory_path() / ("kparb_test_" + name + ".bin");
}

TEST(BinaryLoggerRoundTripTest, WrittenRecordsReadBackIdentically)
{
    const auto path = temporaryLogPath("round_trip");
    {
        BinaryLogger logger(path);
        LogChannel& channel = logger.createChannel(static_cast<std::uint16_t>(SourceIdentifier::Engine));
        for (std::uint32_t index = 0; index < 1000; ++index) {
            OpportunityRecord opportunity{};
            opportunity.pairIndex = index;
            opportunity.kalshiBestPrice = static_cast<Ticks>(index % 1000);
            ASSERT_TRUE(channel.write(opportunity));
        }
        logger.stop();
        EXPECT_EQ(logger.recordsWritten(), 1000U);
    }

    BinaryLogReader reader(path);
    LogRecord record{};
    std::uint32_t expected = 0;
    while (reader.readNext(record)) {
        ASSERT_EQ(record.header.type, RecordType::Opportunity);
        const auto opportunity = record.loadPayload<OpportunityRecord>();
        EXPECT_EQ(opportunity.pairIndex, expected);
        EXPECT_EQ(record.header.sequenceNumber, expected);
        ++expected;
    }
    EXPECT_EQ(expected, 1000U);
    std::filesystem::remove(path);
}

TEST(BinaryLoggerRoundTripTest, BackgroundThreadDrainsMultipleProducers)
{
    const auto path = temporaryLogPath("multi_producer");
    constexpr std::uint32_t recordsPerProducer = 100'000;
    std::atomic<std::uint64_t> observedOpportunities{0};
    {
        BinaryLogger logger(path);
        LogChannel& first = logger.createChannel(1);
        LogChannel& second = logger.createChannel(2);
        logger.setRecordObserver([&observedOpportunities](const LogRecord& record) {
            if (record.header.type == RecordType::Opportunity) {
                observedOpportunities.fetch_add(1, std::memory_order_relaxed);
            }
        });
        logger.start();

        const auto produce = [](LogChannel& channel) {
            for (std::uint32_t index = 0; index < recordsPerProducer;) {
                OpportunityRecord opportunity{};
                opportunity.pairIndex = index;
                if (channel.write(opportunity)) {
                    ++index;
                }
            }
        };
        std::thread firstProducer(produce, std::ref(first));
        std::thread secondProducer(produce, std::ref(second));
        firstProducer.join();
        secondProducer.join();
        logger.stop();
        EXPECT_EQ(logger.recordsWritten(), 2ULL * recordsPerProducer);
    }
    EXPECT_EQ(observedOpportunities.load(), 2ULL * recordsPerProducer);

    BinaryLogReader reader(path);
    std::vector<std::int64_t> lastSequence(3, -1);
    LogRecord record{};
    std::uint64_t total = 0;
    while (reader.readNext(record)) {
        const auto source = record.header.sourceIdentifier;
        ASSERT_TRUE(source == 1 || source == 2);
        EXPECT_GT(static_cast<std::int64_t>(record.header.sequenceNumber), lastSequence[source]);
        lastSequence[source] = record.header.sequenceNumber;
        ++total;
    }
    EXPECT_EQ(total, 2ULL * recordsPerProducer);
    std::filesystem::remove(path);
}

}
}
