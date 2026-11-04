#pragma once

#include "matching/MatchedPair.h"

#include <filesystem>
#include <vector>

namespace kparb {

void saveMatchedPairs(const std::filesystem::path& path, const std::vector<MatchedPair>& pairs);

std::vector<MatchedPair> loadMatchedPairs(const std::filesystem::path& path);

}
