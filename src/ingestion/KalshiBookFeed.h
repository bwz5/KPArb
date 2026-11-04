#pragma once

#include "ingestion/KalshiMessageParser.h"
#include "ingestion/KalshiSigner.h"
#include "ingestion/LadderDirectory.h"
#include "ingestion/WebSocketBookFeed.h"

#include <array>
#include <string>
#include <vector>

namespace kparb {

struct KalshiFeedSettings {
    FeedConnectionSettings connection{"wss://api.elections.kalshi.com/trade-api/ws/v2"};
    std::string apiKeyIdentifier;
    std::size_t tickersPerSubscription = 100;
};

std::vector<std::string> buildKalshiSubscriptionMessages(const std::vector<std::string>& tickers, std::size_t tickersPerSubscription);

class KalshiBookFeed final : public WebSocketBookFeed {
public:
    KalshiBookFeed(KalshiFeedSettings feedSettings, const KalshiSigner& requestSigner, const LadderDirectory& ladderDirectory,
        LadderUpdateQueue& updateQueue, LogChannel& eventChannel);
    ~KalshiBookFeed() override;

private:
    std::vector<std::pair<std::string, std::string>> connectionHeaders() override;
    std::vector<std::string> subscriptionMessages() override;
    void handleText(std::string_view text, std::int64_t receivedAtNanos) override;
    void onConnectionOpened() override;

    void checkSequence(const KalshiParseResult& result) noexcept;

    static constexpr std::size_t trackedSubscriptionCount = 256;

    KalshiFeedSettings settings;
    const KalshiSigner& signer;
    KalshiMessageParser parser;
    std::vector<std::string> subscriptions;
    std::string signedPath;
    std::array<std::int64_t, trackedSubscriptionCount> lastSequenceBySubscription{};
};

}
