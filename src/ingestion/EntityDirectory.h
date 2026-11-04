#pragma once

#include "ingestion/MarketInfo.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kparb {

struct Entity {
    std::string code;
    std::string name;
};

struct EntityAlias {
    std::vector<std::string> words;
    std::size_t entityIndex = 0;
    char trailingCharacter = 0;
};

struct EntityLeague {
    std::string name;
    std::string kalshiSeriesPrefix;
    std::string polymarketSlugPrefix;
    std::vector<Entity> entities;
    std::unordered_map<std::string, std::size_t> entityIndexByCode;
    std::unordered_map<std::string, std::vector<EntityAlias>> aliasesByFirstWord;
};

struct CanonicalText {
    std::string text;
    std::vector<std::size_t> mentionedEntities;
};

class EntityDirectory {
public:
    static EntityDirectory parse(const std::string& json);
    static EntityDirectory load(const std::filesystem::path& path);

    std::size_t leagueCount() const noexcept;
    std::size_t entityCount() const noexcept;
    const EntityLeague* leagueOf(const MarketInfo& market) const noexcept;
    CanonicalText canonicalize(const EntityLeague& league, std::string_view text) const;
    void annotate(MarketInfo& market) const;
    void annotate(std::vector<MarketInfo>& markets) const;

private:
    static std::string keyFor(const EntityLeague& league, std::size_t entityIndex);
    static const std::size_t* findCode(const EntityLeague& league, std::string_view code);
    static std::vector<std::size_t> kalshiEventEntities(const EntityLeague& league, std::string_view eventTicker);
    static std::string kalshiSubjectKey(const EntityLeague& league, std::string_view marketTicker);

    std::vector<EntityLeague> leagues;
};

}
