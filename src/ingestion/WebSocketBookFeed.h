#pragma once

#include "arbitrage/LadderUpdate.h"
#include "ingestion/UpdatePublisher.h"
#include "logging/LogChannel.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace ix {
class WebSocket;
struct WebSocketMessage;
}

namespace kparb {

struct FeedConnectionSettings {
    std::string url;
    int protocolPingIntervalSeconds = 20;
    int textPingIntervalSeconds = 0;
    int maximumBackoffSeconds = 30;
    int handshakeTimeoutSeconds = 15;
};

class WebSocketBookFeed {
public:
    WebSocketBookFeed(Venue feedVenue, FeedConnectionSettings connectionSettings, LadderUpdateQueue& updateQueue, LogChannel& eventChannel);
    virtual ~WebSocketBookFeed();

    WebSocketBookFeed(const WebSocketBookFeed&) = delete;
    WebSocketBookFeed& operator=(const WebSocketBookFeed&) = delete;

    void start();
    void stop();

    bool hasAuthenticationFailed() const noexcept;
    bool isConnected() const noexcept;

protected:
    virtual std::vector<std::pair<std::string, std::string>> connectionHeaders() = 0;
    virtual std::vector<std::string> subscriptionMessages() = 0;
    virtual void handleText(std::string_view text, std::int64_t receivedAtNanos) = 0;
    virtual void onConnectionOpened() {}
    virtual std::string textPingMessage() const;

    void requestResync() noexcept;
    UpdatePublisher& publisher() noexcept;
    const FeedConnectionSettings& connectionSettings() const noexcept;

private:
    void supervise();
    void handleSocketMessage(const ix::WebSocketMessage& message);
    bool sleepUnlessStopped(int milliseconds);

    FeedConnectionSettings settings;
    UpdatePublisher updatePublisher;
    std::atomic<ix::WebSocket*> activeSocket{nullptr};
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> isStopping{false};
    std::atomic<bool> connectionEnded{false};
    std::atomic<bool> connectionOpen{false};
    std::atomic<bool> resyncRequested{false};
    std::atomic<bool> authenticationFailed{false};
    std::atomic<int> lastHttpStatus{0};
    std::thread supervisor;
};

}
