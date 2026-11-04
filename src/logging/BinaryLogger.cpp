#include "logging/BinaryLogger.h"

#include "common/Clock.h"
#include "common/Prefetch.h"
#include "logging/LogFileFormat.h"

#include <chrono>
#include <stdexcept>

namespace kparb {

BinaryLogger::BinaryLogger(std::filesystem::path outputPath)
    : path(std::move(outputPath))
    , writeBuffer(bufferRecordCapacity)
{
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        throw std::runtime_error("Unable to open log file " + path.string());
    }
    const LogFileHeader header{
        logFileMagic,
        logFileFormatVersion,
        static_cast<std::uint32_t>(sizeof(LogRecord)),
        wallClockNanoseconds(),
        steadyNanoseconds(),
    };
    std::fwrite(&header, sizeof(header), 1, file);
}

BinaryLogger::~BinaryLogger()
{
    stop();
    if (file != nullptr) {
        std::fclose(file);
    }
}

LogChannel& BinaryLogger::createChannel(std::uint16_t sourceIdentifier)
{
    if (worker.joinable()) {
        throw std::logic_error("Channels must be created before the logger starts");
    }
    channels.push_back(std::make_unique<LogChannel>(sourceIdentifier));
    return *channels.back();
}

void BinaryLogger::setRecordObserver(RecordObserver recordObserver)
{
    observer = std::move(recordObserver);
}

void BinaryLogger::start()
{
    if (worker.joinable()) {
        return;
    }
    stopRequested.store(false, std::memory_order_relaxed);
    worker = std::thread([this] { run(); });
}

void BinaryLogger::stop()
{
    if (worker.joinable()) {
        stopRequested.store(true, std::memory_order_release);
        worker.join();
    }
    while (drainOnce() > 0) {
    }
    flush();
}

std::size_t BinaryLogger::drainOnce()
{
    std::size_t drained = 0;
    for (auto& channel : channels) {
        while (true) {
            if (bufferedRecordCount + drainBatchSize > writeBuffer.size()) {
                writeBufferedRecords();
            }
            LogRecord* destination = writeBuffer.data() + bufferedRecordCount;
            const std::size_t count = channel->records().tryPopBatch(destination, drainBatchSize);
            if (count == 0) {
                break;
            }
            if (observer) {
                for (std::size_t index = 0; index < count; ++index) {
                    if (index + 1 < count) {
                        prefetchForRead(destination + index + 1);
                    }
                    observer(destination[index]);
                }
            }
            bufferedRecordCount += count;
            drained += count;
            if (count < drainBatchSize) {
                break;
            }
        }
    }
    return drained;
}

void BinaryLogger::flush()
{
    writeBufferedRecords();
    if (file != nullptr) {
        std::fflush(file);
    }
}

void BinaryLogger::writeBufferedRecords()
{
    if (bufferedRecordCount == 0 || file == nullptr) {
        return;
    }
    std::fwrite(writeBuffer.data(), sizeof(LogRecord), bufferedRecordCount, file);
    totalRecordsWritten.fetch_add(bufferedRecordCount, std::memory_order_relaxed);
    bufferedRecordCount = 0;
}

void BinaryLogger::run()
{
    using namespace std::chrono_literals;
    auto lastFlush = std::chrono::steady_clock::now();
    while (!stopRequested.load(std::memory_order_acquire)) {
        const std::size_t drained = drainOnce();
        const auto now = std::chrono::steady_clock::now();
        if (now - lastFlush >= 250ms) {
            flush();
            lastFlush = now;
        }
        if (drained == 0) {
            std::this_thread::sleep_for(1ms);
        }
    }
}

std::uint64_t BinaryLogger::recordsWritten() const noexcept
{
    return totalRecordsWritten.load(std::memory_order_relaxed);
}

std::uint64_t BinaryLogger::droppedRecordCount() const noexcept
{
    std::uint64_t total = 0;
    for (const auto& channel : channels) {
        total += channel->droppedRecordCount();
    }
    return total;
}

const std::filesystem::path& BinaryLogger::outputPath() const noexcept
{
    return path;
}

}
