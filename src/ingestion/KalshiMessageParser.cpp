#include "ingestion/KalshiMessageParser.h"

#include "common/DecimalParsing.h"

#include <limits>

namespace kparb {

namespace {

std::int32_t clampQuantity(std::int64_t quantity) noexcept
{
    if (quantity > std::numeric_limits<std::int32_t>::max()) {
        return std::numeric_limits<std::int32_t>::max();
    }
    if (quantity < std::numeric_limits<std::int32_t>::min()) {
        return std::numeric_limits<std::int32_t>::min();
    }
    return static_cast<std::int32_t>(quantity);
}

bool readPrice(simdjson::dom::element value, std::int64_t& price) noexcept
{
    std::int64_t cents = 0;
    if (value.get_int64().get(cents) == simdjson::SUCCESS) {
        price = cents * 10;
        return isValidPrice(price);
    }
    std::string_view dollars;
    if (value.get_string().get(dollars) == simdjson::SUCCESS) {
        const auto parsed = parseScaledDecimal(dollars, priceDecimalDigits, Rounding::Down);
        if (parsed && isValidPrice(*parsed)) {
            price = *parsed;
            return true;
        }
    }
    return false;
}

bool readCount(simdjson::dom::element value, std::int64_t& quantity) noexcept
{
    std::int64_t contracts = 0;
    if (value.get_int64().get(contracts) == simdjson::SUCCESS) {
        quantity = contracts * quantityUnitsPerContract;
        return true;
    }
    double approximate = 0;
    if (value.get_double().get(approximate) == simdjson::SUCCESS) {
        quantity = static_cast<std::int64_t>(approximate * quantityUnitsPerContract);
        return true;
    }
    std::string_view text;
    if (value.get_string().get(text) == simdjson::SUCCESS) {
        const auto parsed = parseScaledDecimal(text, quantityDecimalDigits, Rounding::Down);
        if (parsed) {
            quantity = *parsed;
            return true;
        }
    }
    return false;
}

bool findLevelArray(simdjson::dom::object message, std::string_view fixedPointKey, std::string_view dollarsKey,
    std::string_view centsKey, simdjson::dom::array& levels) noexcept
{
    return message[fixedPointKey].get_array().get(levels) == simdjson::SUCCESS
        || message[dollarsKey].get_array().get(levels) == simdjson::SUCCESS
        || message[centsKey].get_array().get(levels) == simdjson::SUCCESS;
}

}

KalshiMessageParser::KalshiMessageParser(const LadderDirectory& ladderDirectory, std::size_t initialCapacity)
    : directory(ladderDirectory)
    , document(initialCapacity)
{
}

KalshiParseResult KalshiMessageParser::parse(std::string_view message, std::int64_t receivedAtNanos, LadderUpdateSink sink)
{
    KalshiParseResult result;
    simdjson::dom::element root;
    if (document.parse(message, root) != simdjson::SUCCESS) {
        result.kind = KalshiMessageKind::Malformed;
        return result;
    }
    std::string_view type;
    if (root["type"].get_string().get(type) != simdjson::SUCCESS) {
        result.kind = KalshiMessageKind::Malformed;
        return result;
    }
    static_cast<void>(root["sid"].get_int64().get(result.subscriptionIdentifier));
    static_cast<void>(root["seq"].get_int64().get(result.sequenceNumber));

    simdjson::dom::object body;
    const bool hasBody = root["msg"].get_object().get(body) == simdjson::SUCCESS;

    if (type == "subscribed") {
        result.kind = KalshiMessageKind::Subscribed;
        if (hasBody) {
            static_cast<void>(body["sid"].get_int64().get(result.subscriptionIdentifier));
        }
        return result;
    }
    if (type == "error") {
        result.kind = KalshiMessageKind::Error;
        if (hasBody) {
            static_cast<void>(body["code"].get_int64().get(result.errorCode));
        }
        return result;
    }
    if (type != "orderbook_snapshot" && type != "orderbook_delta") {
        return result;
    }
    if (!hasBody) {
        result.kind = KalshiMessageKind::Malformed;
        return result;
    }
    std::string_view ticker;
    if (body["market_ticker"].get_string().get(ticker) != simdjson::SUCCESS) {
        result.kind = KalshiMessageKind::Malformed;
        return result;
    }
    const auto* targets = directory.findKalshiMarket(ticker);
    if (targets == nullptr) {
        result.kind = KalshiMessageKind::UnknownMarket;
        return result;
    }

    if (type == "orderbook_snapshot") {
        result.kind = KalshiMessageKind::Snapshot;
        for (const KalshiLadderTargets& target : *targets) {
            sink(LadderUpdate{receivedAtNanos, target.buyYesLadder, 0, 0, UpdateKind::ClearLadder, Venue::Kalshi});
            sink(LadderUpdate{receivedAtNanos, target.buyNoLadder, 0, 0, UpdateKind::ClearLadder, Venue::Kalshi});
            result.updatesProduced += 2;
        }
        const auto emitSide = [&](simdjson::dom::array levels, bool isYesBids) {
            for (simdjson::dom::element level : levels) {
                simdjson::dom::array pair;
                if (level.get_array().get(pair) != simdjson::SUCCESS || pair.size() < 2) {
                    continue;
                }
                std::int64_t bidPrice = 0;
                std::int64_t quantity = 0;
                if (!readPrice(pair.at(0).value_unsafe(), bidPrice) || !readCount(pair.at(1).value_unsafe(), quantity)) {
                    continue;
                }
                const Ticks askPrice = complementPrice(static_cast<Ticks>(bidPrice));
                for (const KalshiLadderTargets& target : *targets) {
                    const std::uint32_t ladderIndex = isYesBids ? target.buyNoLadder : target.buyYesLadder;
                    sink(LadderUpdate{receivedAtNanos, ladderIndex, clampQuantity(quantity), askPrice, UpdateKind::SetLevel, Venue::Kalshi});
                    ++result.updatesProduced;
                }
            }
        };
        simdjson::dom::array levels;
        if (findLevelArray(body, "yes_dollars_fp", "yes_dollars", "yes", levels)) {
            emitSide(levels, true);
        }
        if (findLevelArray(body, "no_dollars_fp", "no_dollars", "no", levels)) {
            emitSide(levels, false);
        }
        return result;
    }

    result.kind = KalshiMessageKind::Delta;
    std::string_view side;
    if (body["side"].get_string().get(side) != simdjson::SUCCESS || (side != "yes" && side != "no")) {
        result.kind = KalshiMessageKind::Malformed;
        return result;
    }
    std::int64_t bidPrice = 0;
    simdjson::dom::element priceValue;
    if (body["price_dollars"].get(priceValue) != simdjson::SUCCESS && body["price"].get(priceValue) != simdjson::SUCCESS) {
        result.kind = KalshiMessageKind::Malformed;
        return result;
    }
    if (!readPrice(priceValue, bidPrice)) {
        result.kind = KalshiMessageKind::Malformed;
        return result;
    }
    std::int64_t quantityChange = 0;
    simdjson::dom::element deltaValue;
    if (body["delta_fp"].get(deltaValue) != simdjson::SUCCESS && body["delta"].get(deltaValue) != simdjson::SUCCESS) {
        result.kind = KalshiMessageKind::Malformed;
        return result;
    }
    if (!readCount(deltaValue, quantityChange)) {
        result.kind = KalshiMessageKind::Malformed;
        return result;
    }
    const bool isYesBid = side == "yes";
    const Ticks askPrice = complementPrice(static_cast<Ticks>(bidPrice));
    for (const KalshiLadderTargets& target : *targets) {
        const std::uint32_t ladderIndex = isYesBid ? target.buyNoLadder : target.buyYesLadder;
        sink(LadderUpdate{receivedAtNanos, ladderIndex, clampQuantity(quantityChange), askPrice, UpdateKind::AddToLevel, Venue::Kalshi});
        ++result.updatesProduced;
    }
    return result;
}

}
