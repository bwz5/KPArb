#include "ingestion/PolymarketMessageParser.h"

#include "common/DecimalParsing.h"

#include <limits>

namespace kparb {

namespace {

bool readDecimalString(simdjson::dom::object object, std::string_view key, int digits, Rounding rounding, std::int64_t& value) noexcept
{
    std::string_view text;
    if (object[key].get_string().get(text) == simdjson::SUCCESS) {
        const auto parsed = parseScaledDecimal(text, digits, rounding);
        if (parsed) {
            value = *parsed;
            return true;
        }
        return false;
    }
    double number = 0;
    if (object[key].get_double().get(number) == simdjson::SUCCESS) {
        double scale = 1;
        for (int digit = 0; digit < digits; ++digit) {
            scale *= 10;
        }
        value = static_cast<std::int64_t>(number * scale + (rounding == Rounding::Up ? 0.999999 : 0.0));
        return true;
    }
    return false;
}

std::int32_t clampQuantity(std::int64_t quantity) noexcept
{
    if (quantity > std::numeric_limits<std::int32_t>::max()) {
        return std::numeric_limits<std::int32_t>::max();
    }
    return quantity < 0 ? 0 : static_cast<std::int32_t>(quantity);
}

}

PolymarketMessageParser::PolymarketMessageParser(const LadderDirectory& ladderDirectory, std::size_t initialCapacity)
    : directory(ladderDirectory)
    , document(initialCapacity)
{
}

PolymarketParseResult PolymarketMessageParser::parse(std::string_view message, std::int64_t receivedAtNanos, LadderUpdateSink sink)
{
    PolymarketParseResult result;
    if (message.empty() || (message.front() != '{' && message.front() != '[')) {
        return result;
    }
    simdjson::dom::element root;
    if (document.parse(message, root) != simdjson::SUCCESS) {
        result.isMalformed = true;
        return result;
    }
    simdjson::dom::array events;
    if (root.get_array().get(events) == simdjson::SUCCESS) {
        for (simdjson::dom::element element : events) {
            simdjson::dom::object event;
            if (element.get_object().get(event) == simdjson::SUCCESS) {
                parseEvent(event, receivedAtNanos, sink, result);
            }
        }
        return result;
    }
    simdjson::dom::object event;
    if (root.get_object().get(event) == simdjson::SUCCESS) {
        parseEvent(event, receivedAtNanos, sink, result);
        return result;
    }
    result.isMalformed = true;
    return result;
}

void PolymarketMessageParser::parseEvent(simdjson::dom::object event, std::int64_t receivedAtNanos, LadderUpdateSink sink,
    PolymarketParseResult& result)
{
    std::string_view eventType;
    if (event["event_type"].get_string().get(eventType) != simdjson::SUCCESS) {
        return;
    }
    if (eventType == "book") {
        parseBook(event, receivedAtNanos, sink, result);
        return;
    }
    if (eventType != "price_change") {
        return;
    }
    ++result.priceChangeCount;
    simdjson::dom::array changes;
    if (event["price_changes"].get_array().get(changes) == simdjson::SUCCESS) {
        for (simdjson::dom::element element : changes) {
            simdjson::dom::object change;
            std::string_view tokenIdentifier;
            if (element.get_object().get(change) == simdjson::SUCCESS
                && change["asset_id"].get_string().get(tokenIdentifier) == simdjson::SUCCESS) {
                parseChange(tokenIdentifier, change, receivedAtNanos, sink, result);
            }
        }
        return;
    }
    std::string_view tokenIdentifier;
    if (event["asset_id"].get_string().get(tokenIdentifier) == simdjson::SUCCESS
        && event["changes"].get_array().get(changes) == simdjson::SUCCESS) {
        for (simdjson::dom::element element : changes) {
            simdjson::dom::object change;
            if (element.get_object().get(change) == simdjson::SUCCESS) {
                parseChange(tokenIdentifier, change, receivedAtNanos, sink, result);
            }
        }
    }
}

void PolymarketMessageParser::parseBook(simdjson::dom::object event, std::int64_t receivedAtNanos, LadderUpdateSink sink,
    PolymarketParseResult& result)
{
    std::string_view tokenIdentifier;
    if (event["asset_id"].get_string().get(tokenIdentifier) != simdjson::SUCCESS) {
        return;
    }
    const auto* ladders = directory.findPolymarketToken(tokenIdentifier);
    if (ladders == nullptr) {
        return;
    }
    ++result.snapshotCount;
    for (const std::uint32_t ladderIndex : *ladders) {
        sink(LadderUpdate{receivedAtNanos, ladderIndex, 0, 0, UpdateKind::ClearLadder, Venue::Polymarket});
        ++result.updatesProduced;
    }
    simdjson::dom::array asks;
    if (event["asks"].get_array().get(asks) != simdjson::SUCCESS && event["sells"].get_array().get(asks) != simdjson::SUCCESS) {
        return;
    }
    for (simdjson::dom::element element : asks) {
        simdjson::dom::object level;
        if (element.get_object().get(level) != simdjson::SUCCESS) {
            continue;
        }
        std::int64_t price = 0;
        std::int64_t quantity = 0;
        if (!readDecimalString(level, "price", priceDecimalDigits, Rounding::Up, price) || !isValidPrice(price)
            || !readDecimalString(level, "size", quantityDecimalDigits, Rounding::Down, quantity)) {
            continue;
        }
        for (const std::uint32_t ladderIndex : *ladders) {
            sink(LadderUpdate{receivedAtNanos, ladderIndex, clampQuantity(quantity), static_cast<Ticks>(price),
                UpdateKind::SetLevel, Venue::Polymarket});
            ++result.updatesProduced;
        }
    }
}

void PolymarketMessageParser::parseChange(std::string_view tokenIdentifier, simdjson::dom::object change,
    std::int64_t receivedAtNanos, LadderUpdateSink sink, PolymarketParseResult& result)
{
    std::string_view side;
    if (change["side"].get_string().get(side) != simdjson::SUCCESS || (side != "SELL" && side != "sell")) {
        return;
    }
    const auto* ladders = directory.findPolymarketToken(tokenIdentifier);
    if (ladders == nullptr) {
        return;
    }
    std::int64_t price = 0;
    std::int64_t quantity = 0;
    if (!readDecimalString(change, "price", priceDecimalDigits, Rounding::Up, price) || !isValidPrice(price)
        || !readDecimalString(change, "size", quantityDecimalDigits, Rounding::Down, quantity)) {
        return;
    }
    for (const std::uint32_t ladderIndex : *ladders) {
        sink(LadderUpdate{receivedAtNanos, ladderIndex, clampQuantity(quantity), static_cast<Ticks>(price),
            UpdateKind::SetLevel, Venue::Polymarket});
        ++result.updatesProduced;
    }
}

}
