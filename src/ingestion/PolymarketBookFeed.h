#pragma once

#include "ingestion/LadderDirectory.h"
#include "ingestion/PolymarketMessageParser.h"
#include "ingestion/WebSocketBookFeed.h"

#include <string>
#include <vector>

namespace kparb {

struct PolymarketFeedSettings {
    FeedConnectionSettings connection{"wss://ws-subscriptions-clob.polymarket.com/ws/market", 20, 10};
    std::size_t tokensPerSubscription = 500;
};

std::vector<std::string> buildPolymarketSubscriptionMessages(const std::vector<std::string>& tokens, std::size_t tokensPerSubscription);

class PolymarketBookFeed final : public WebSocketBookFeed {
public:
    PolymarketBookFeed(PolymarketFeedSettings feedSettings, const LadderDirectory& ladderDirectory, LadderUpdateQueue& updateQueue,
        LogChannel& eventChannel);
    ~PolymarketBookFeed() override;

private:
    std::vector<std::pair<std::string, std::string>> connectionHeaders() override;
    std::vector<std::string> subscriptionMessages() override;
    void handleText(std::string_view text, std::int64_t receivedAtNanos) override;
    std::string textPingMessage() const override;

    PolymarketFeedSettings settings;
    PolymarketMessageParser parser;
    std::vector<std::string> subscriptions;
};

}
