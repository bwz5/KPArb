#include "ingestion/WebSocketBookFeed.h"

#include "common/Clock.h"
#include "ingestion/UrlEncoding.h"

#include <ixwebsocket/IXWebSocket.h>

#include <algorithm>
#include <chrono>

namespace kparb {

WebSocketBookFeed::WebSocketBookFeed(Venue feedVenue, FeedConnectionSettings connectionSettings, LadderUpdateQueue& updateQueue,
    LogChannel& eventChannel)
    : settings(std::move(connectionSettings))
    , updatePublisher(feedVenue, updateQueue, eventChannel)
{
}

WebSocketBookFeed::~WebSocketBookFeed()
{
    stop();
}

void WebSocketBookFeed::start()
{
    if (supervisor.joinable()) {
        return;
    }
    stopRequested.store(false, std::memory_order_release);
    supervisor = std::thread([this] { supervise(); });
}

void WebSocketBookFeed::stop()
{
    stopRequested.store(true, std::memory_order_release);
    if (supervisor.joinable()) {
        supervisor.join();
    }
}

bool WebSocketBookFeed::hasAuthenticationFailed() const noexcept
{
    return authenticationFailed.load(std::memory_order_acquire);
}

bool WebSocketBookFeed::isConnected() const noexcept
{
    return connectionOpen.load(std::memory_order_acquire);
}

std::string WebSocketBookFeed::textPingMessage() const
{
    return {};
}

void WebSocketBookFeed::requestResync() noexcept
{
    resyncRequested.store(true, std::memory_order_release);
}

UpdatePublisher& WebSocketBookFeed::publisher() noexcept
{
    return updatePublisher;
}

const FeedConnectionSettings& WebSocketBookFeed::connectionSettings() const noexcept
{
    return settings;
}

bool WebSocketBookFeed::sleepUnlessStopped(int milliseconds)
{
    const int step = 100;
    for (int waited = 0; waited < milliseconds; waited += step) {
        if (stopRequested.load(std::memory_order_acquire)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(step, milliseconds - waited)));
    }
    return !stopRequested.load(std::memory_order_acquire);
}

void WebSocketBookFeed::supervise()
{
    using Clock = std::chrono::steady_clock;
    int backoffSeconds = 1;
    const std::string pingText = textPingMessage();

    while (!stopRequested.load(std::memory_order_acquire)) {
        ix::WebSocketHttpHeaders headers;
        headers["Origin"] = extractUrlOrigin(settings.url);
        try {
            for (auto& [name, value] : connectionHeaders()) {
                headers[name] = value;
            }
        } catch (...) {
            updatePublisher.logEvent(FeedEventCode::AuthenticationFailed, 0, 0);
            authenticationFailed.store(true, std::memory_order_release);
            break;
        }

        auto socket = std::make_unique<ix::WebSocket>();
        socket->setUrl(settings.url);
        socket->setExtraHeaders(headers);
        socket->disableAutomaticReconnection();
        socket->setPingInterval(settings.protocolPingIntervalSeconds);
        socket->setHandshakeTimeout(settings.handshakeTimeoutSeconds);
        socket->setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) { handleSocketMessage(*message); });

        connectionEnded.store(false, std::memory_order_release);
        resyncRequested.store(false, std::memory_order_release);
        isStopping.store(false, std::memory_order_release);
        activeSocket.store(socket.get(), std::memory_order_release);

        const auto connectedAt = Clock::now();
        auto lastTextPing = connectedAt;
        socket->start();

        while (!stopRequested.load(std::memory_order_acquire) && !connectionEnded.load(std::memory_order_acquire)
            && !resyncRequested.load(std::memory_order_acquire) && !updatePublisher.takeOverflowSignal()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const auto now = Clock::now();
            if (!pingText.empty() && settings.textPingIntervalSeconds > 0 && connectionOpen.load(std::memory_order_acquire)
                && now - lastTextPing >= std::chrono::seconds(settings.textPingIntervalSeconds)) {
                socket->sendText(pingText);
                lastTextPing = now;
            }
        }

        isStopping.store(true, std::memory_order_release);
        socket->stop();
        activeSocket.store(nullptr, std::memory_order_release);
        socket.reset();
        connectionOpen.store(false, std::memory_order_release);

        updatePublisher.invalidateBooks(steadyNanoseconds());
        updatePublisher.logEvent(FeedEventCode::Disconnected, lastHttpStatus.load(std::memory_order_acquire), 0);

        if (stopRequested.load(std::memory_order_acquire) || authenticationFailed.load(std::memory_order_acquire)) {
            break;
        }
        if (Clock::now() - connectedAt > std::chrono::seconds(60)) {
            backoffSeconds = 1;
        }
        updatePublisher.logEvent(FeedEventCode::Reconnecting, backoffSeconds, 0);
        if (!sleepUnlessStopped(backoffSeconds * 1000)) {
            break;
        }
        backoffSeconds = std::min(backoffSeconds * 2, settings.maximumBackoffSeconds);
    }
}

void WebSocketBookFeed::handleSocketMessage(const ix::WebSocketMessage& message)
{
    if (isStopping.load(std::memory_order_acquire)) {
        return;
    }
    const std::int64_t receivedAtNanos = steadyNanoseconds();
    switch (message.type) {
    case ix::WebSocketMessageType::Message:
        handleText(message.str, receivedAtNanos);
        break;
    case ix::WebSocketMessageType::Open: {
        connectionOpen.store(true, std::memory_order_release);
        lastHttpStatus.store(101, std::memory_order_release);
        onConnectionOpened();
        updatePublisher.logEvent(FeedEventCode::Connected, 0, 0);
        ix::WebSocket* socket = activeSocket.load(std::memory_order_acquire);
        if (socket != nullptr) {
            std::int64_t sent = 0;
            for (const std::string& subscription : subscriptionMessages()) {
                socket->sendText(subscription);
                ++sent;
            }
            updatePublisher.logEvent(FeedEventCode::SubscriptionSent, sent, 0);
        }
        break;
    }
    case ix::WebSocketMessageType::Close:
        connectionOpen.store(false, std::memory_order_release);
        updatePublisher.logEvent(FeedEventCode::Disconnected, message.closeInfo.code, message.closeInfo.remote ? 1 : 0);
        connectionEnded.store(true, std::memory_order_release);
        break;
    case ix::WebSocketMessageType::Error: {
        const int status = message.errorInfo.http_status;
        lastHttpStatus.store(status, std::memory_order_release);
        if (status == 401 || status == 403) {
            authenticationFailed.store(true, std::memory_order_release);
            updatePublisher.logEvent(FeedEventCode::AuthenticationFailed, status, 0);
        } else {
            updatePublisher.logEvent(FeedEventCode::ConnectionError, status, static_cast<std::int64_t>(message.errorInfo.retries));
        }
        connectionEnded.store(true, std::memory_order_release);
        break;
    }
    default:
        break;
    }
}

}
