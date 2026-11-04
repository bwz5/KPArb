#pragma once

#include "ingestion/MarketInfo.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kparb {

struct TeamNames {
    std::string code;
    std::vector<std::string> lowercaseNames;
};

struct LeagueTeams {
    std::string name;
    std::string kalshiSeriesPrefix;
    std::string polymarketSlugPrefix;
    std::vector<TeamNames> teams;
};

class TeamDirectory {
public:
    static TeamDirectory parse(const std::string& json);
    static TeamDirectory load(const std::filesystem::path& path);

    std::vector<std::string> teamKeysFor(const MarketInfo& market) const;
    std::string subjectTeamKeyFor(const MarketInfo& market) const;
    void annotate(std::vector<MarketInfo>& markets) const;
    std::size_t leagueCount() const noexcept;

private:
    std::vector<std::string> kalshiTeamKeys(const MarketInfo& market) const;
    std::vector<std::string> polymarketTeamKeys(const MarketInfo& market) const;
    const LeagueTeams* leagueOf(const MarketInfo& market) const noexcept;
    static std::string keyFor(const LeagueTeams& league, std::string_view code);
    static bool isTeamCode(const LeagueTeams& league, std::string_view code) noexcept;
    static std::vector<std::string> mentionedTeamKeys(const LeagueTeams& league, const std::string& text);

    std::vector<LeagueTeams> leagues;
};

}
