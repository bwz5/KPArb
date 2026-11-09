add_library(kparb_application STATIC
    Application.cpp
    ApplicationConfig.cpp
    BackgroundMatcher.cpp
    MatchedPairFile.cpp
)
target_link_libraries(kparb_application
    PUBLIC kparb_common kparb_logging kparb_arbitrage kparb_ingestion kparb_matching kparb_execution
    PRIVATE nlohmann_json::nlohmann_json
)

add_executable(kparb main.cpp)
target_link_libraries(kparb PRIVATE kparb_application)
