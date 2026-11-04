#include "ingestion/PolymarketBookFeed.h"

#include <nlohmann/json.hpp>

namespace kparb {

std::vector<std::string> buildPolymarketSubscriptionMessages(const std::vector<std::string>& tokens, std::size_t tokensPerSubscription)
{
    std::vector<std::string> messages;
    const std::size_t chunkSize = tokensPerSubscription == 0 ? tokens.size() : tokensPerSubscription;
    for (std::size_t offset = 0; offset < tokens.size(); offset += chunkSize) {
        nlohmann::json chunk = nlohmann::json::array();
        for (std::size_t index = offset; index < tokens.size() && index < offset + chunkSize; ++index) {
            chunk.push_back(tokens[index]);
        }
        nlohmann::json command{{"assets_ids", chunk}};
        if (offset == 0) {
            command["type"] = "market";
        } else {
            command["operation"] = "subscribe";
        }
        messages.push_back(command.dump());
    }
    return messages;
}

PolymarketBookFeed::PolymarketBookFeed(PolymarketFeedSettings feedSettings, const LadderDirectory& ladderDirectory,
    LadderUpdateQueue& updateQueue, LogChannel& eventChannel)
    : WebSocketBookFeed(Venue::Polymarket, feedSettings.connection, updateQueue, eventChannel)
    , settings(std::move(feedSettings))
    , parser(ladderDirectory)
    , subscriptions(buildPolymarketSubscriptionMessages(ladderDirectory.polymarketTokens(), settings.tokensPerSubscription))
{
}

PolymarketBookFeed::~PolymarketBookFeed()
{
    stop();
}

std::vector<std::pair<std::string, std::string>> PolymarketBookFeed::connectionHeaders()
{
    return {};
}

std::vector<std::string> PolymarketBookFeed::subscriptionMessages()
{
    return subscriptions;
}

std::string PolymarketBookFeed::textPingMessage() const
{
    return "PING";
}

void PolymarketBookFeed::handleText(std::string_view text, std::int64_t receivedAtNanos)
{
    auto deliver = [this](const LadderUpdate& update) { publisher().publish(update); };
    const PolymarketParseResult result = parser.parse(text, receivedAtNanos, LadderUpdateSink(deliver));
    if (result.isMalformed) {
        publisher().logEvent(FeedEventCode::ParseError, static_cast<std::int64_t>(text.size()), 0);
    } else if (result.snapshotCount > 0) {
        publisher().logEvent(FeedEventCode::SnapshotReceived, result.snapshotCount, result.updatesProduced);
    }
}

}
