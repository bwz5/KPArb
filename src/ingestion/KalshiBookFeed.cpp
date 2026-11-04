#include "ingestion/KalshiBookFeed.h"

#include "common/Clock.h"
#include "ingestion/UrlEncoding.h"

#include <nlohmann/json.hpp>

namespace kparb {

std::vector<std::string> buildKalshiSubscriptionMessages(const std::vector<std::string>& tickers, std::size_t tickersPerSubscription)
{
    std::vector<std::string> messages;
    const std::size_t chunkSize = tickersPerSubscription == 0 ? tickers.size() : tickersPerSubscription;
    std::int64_t commandIdentifier = 1;
    for (std::size_t offset = 0; offset < tickers.size(); offset += chunkSize) {
        nlohmann::json chunk = nlohmann::json::array();
        for (std::size_t index = offset; index < tickers.size() && index < offset + chunkSize; ++index) {
            chunk.push_back(tickers[index]);
        }
        nlohmann::json command{
            {"id", commandIdentifier++},
            {"cmd", "subscribe"},
            {"params", {{"channels", {"orderbook_delta"}}, {"market_tickers", chunk}}},
        };
        messages.push_back(command.dump());
    }
    return messages;
}

KalshiBookFeed::KalshiBookFeed(KalshiFeedSettings feedSettings, const KalshiSigner& requestSigner, const LadderDirectory& ladderDirectory,
    LadderUpdateQueue& updateQueue, LogChannel& eventChannel)
    : WebSocketBookFeed(Venue::Kalshi, feedSettings.connection, updateQueue, eventChannel)
    , settings(std::move(feedSettings))
    , signer(requestSigner)
    , parser(ladderDirectory)
    , subscriptions(buildKalshiSubscriptionMessages(ladderDirectory.kalshiTickers(), settings.tickersPerSubscription))
    , signedPath(extractUrlPath(settings.connection.url))
{
}

KalshiBookFeed::~KalshiBookFeed()
{
    stop();
}

std::vector<std::pair<std::string, std::string>> KalshiBookFeed::connectionHeaders()
{
    return signer.authenticationHeaders(settings.apiKeyIdentifier, "GET", signedPath, wallClockMilliseconds());
}

std::vector<std::string> KalshiBookFeed::subscriptionMessages()
{
    return subscriptions;
}

void KalshiBookFeed::onConnectionOpened()
{
    lastSequenceBySubscription.fill(0);
}

void KalshiBookFeed::handleText(std::string_view text, std::int64_t receivedAtNanos)
{
    auto deliver = [this](const LadderUpdate& update) { publisher().publish(update); };
    const KalshiParseResult result = parser.parse(text, receivedAtNanos, LadderUpdateSink(deliver));
    switch (result.kind) {
    case KalshiMessageKind::Snapshot:
        publisher().logEvent(FeedEventCode::SnapshotReceived, result.subscriptionIdentifier, result.updatesProduced);
        checkSequence(result);
        break;
    case KalshiMessageKind::Delta:
    case KalshiMessageKind::UnknownMarket:
        checkSequence(result);
        break;
    case KalshiMessageKind::Subscribed:
        publisher().logEvent(FeedEventCode::SubscriptionConfirmed, result.subscriptionIdentifier, 0);
        break;
    case KalshiMessageKind::Error:
        publisher().logEvent(FeedEventCode::ServerError, result.errorCode, 0);
        break;
    case KalshiMessageKind::Malformed:
        publisher().logEvent(FeedEventCode::ParseError, static_cast<std::int64_t>(text.size()), 0);
        break;
    case KalshiMessageKind::Ignored:
        break;
    }
}

void KalshiBookFeed::checkSequence(const KalshiParseResult& result) noexcept
{
    if (result.subscriptionIdentifier < 0 || result.sequenceNumber < 0
        || result.subscriptionIdentifier >= static_cast<std::int64_t>(trackedSubscriptionCount)) {
        return;
    }
    std::int64_t& lastSequence = lastSequenceBySubscription[static_cast<std::size_t>(result.subscriptionIdentifier)];
    if (lastSequence != 0 && result.sequenceNumber != lastSequence + 1) {
        publisher().logEvent(FeedEventCode::SequenceGap, result.subscriptionIdentifier, result.sequenceNumber);
        publisher().logEvent(FeedEventCode::ResyncRequested, result.subscriptionIdentifier, 0);
        requestResync();
    }
    lastSequence = result.sequenceNumber;
}

}
