add_library(kparb_matching STATIC
    CandidateFinder.cpp
    ChatCompletionsClient.cpp
    MarketMatcher.cpp
    MatchStore.cpp
    TextNormalizer.cpp
)
target_link_libraries(kparb_matching
    PUBLIC kparb_common kparb_logging kparb_ingestion
    PRIVATE nlohmann_json::nlohmann_json
)
