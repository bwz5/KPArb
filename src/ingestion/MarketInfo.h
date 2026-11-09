#pragma once

#include "common/Venue.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kparb {

struct MarketInfo {
    Venue venue = Venue::Kalshi;
    std::string identifier;
    std::string eventIdentifier;
    std::string eventTitle;
    std::string title;
    std::string subtitle;
    std::string rules;
    std::string rulesSummary;
    std::string marketType;
    std::string yesOutcomeName;
    std::string noOutcomeName;
    std::string yesTokenIdentifier;
    std::string noTokenIdentifier;
    std::int64_t closeTimeSeconds = 0;
    std::int64_t resolutionDeadlineSeconds = 0;
    std::int64_t openTimeSeconds = 0;
    std::int64_t scheduledStartSeconds = 0;
    bool isAcceptingOrders = false;
    double takerFeeRate = 0.0;
    double takerFeeExponent = 1.0;
    double strike = 0.0;
    bool hasStrike = false;
    bool isStrikeFloor = false;
    double signedLine = 0.0;
    std::string entityLeague;
    std::vector<std::string> teamKeys;
    std::string subjectTeamKey;
    std::string yesOutcomeEntityKey;
    std::string noOutcomeEntityKey;
};

}
