#pragma once

#include "logging/LogChannel.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace kparb {

class BinaryLogger {
public:
    using RecordObserver = std::function<void(const LogRecord&)>;

    explicit BinaryLogger(std::filesystem::path outputPath);
    ~BinaryLogger();

    BinaryLogger(const BinaryLogger&) = delete;
    BinaryLogger& operator=(const BinaryLogger&) = delete;

    LogChannel& createChannel(std::uint16_t sourceIdentifier);
    void setRecordObserver(RecordObserver observer);

    void start();
    void stop();

    std::size_t drainOnce();
    void flush();

    std::uint64_t recordsWritten() const noexcept;
    std::uint64_t droppedRecordCount() const noexcept;
    const std::filesystem::path& outputPath() const noexcept;

private:
    void run();
    void writeBufferedRecords();

    static constexpr std::size_t drainBatchSize = 256;
    static constexpr std::size_t bufferRecordCapacity = (1 << 20) / sizeof(LogRecord);

    std::filesystem::path path;
    std::FILE* file = nullptr;
    std::vector<std::unique_ptr<LogChannel>> channels;
    std::vector<LogRecord> writeBuffer;
    std::size_t bufferedRecordCount = 0;
    RecordObserver observer;
    std::atomic<bool> stopRequested{false};
    std::atomic<std::uint64_t> totalRecordsWritten{0};
    std::thread worker;
};

}
