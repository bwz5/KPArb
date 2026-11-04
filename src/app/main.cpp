#include "app/Application.h"
#include "app/ApplicationConfig.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

std::atomic<bool> shutdownRequested{false};

extern "C" void handleShutdownSignal(int)
{
    shutdownRequested.store(true, std::memory_order_release);
}

void printUsage()
{
    std::cout << "usage: kparb [--config path] [--match-only | --use-saved-pairs] [--duration-seconds n]\n"
                 "  --config path          configuration file (default config/config.json, optional)\n"
                 "  --match-only           discover and match markets, save pairs, then exit\n"
                 "  --use-saved-pairs      skip discovery and matching and stream the saved pairs\n"
                 "  --duration-seconds n   stop streaming after n seconds\n";
}

}

int main(int argumentCount, char** arguments)
{
    static_assert(std::atomic<bool>::is_always_lock_free);
    kparb::RunOptions options;
    for (int index = 1; index < argumentCount; ++index) {
        const std::string argument = arguments[index];
        if (argument == "--config" && index + 1 < argumentCount) {
            options.configPath = arguments[++index];
        } else if (argument == "--match-only") {
            options.mode = kparb::RunMode::MatchOnly;
        } else if (argument == "--use-saved-pairs") {
            options.mode = kparb::RunMode::TradeSavedPairs;
        } else if (argument == "--duration-seconds" && index + 1 < argumentCount) {
            options.durationSeconds = std::atoll(arguments[++index]);
        } else if (argument == "--help" || argument == "-h") {
            printUsage();
            return 0;
        } else {
            std::cerr << "unknown argument: " << argument << '\n';
            printUsage();
            return 64;
        }
    }

    std::signal(SIGINT, handleShutdownSignal);
    std::signal(SIGTERM, handleShutdownSignal);

    try {
        kparb::Application application(kparb::loadApplicationConfig(options.configPath), options);
        return application.run(shutdownRequested);
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
