add_library(kparb_execution STATIC
    ExposureBook.cpp
    KalshiOrderClient.cpp
    PaperOrderClient.cpp
    TradeExecutor.cpp
    TradePlanner.cpp
)
target_link_libraries(kparb_execution
    PUBLIC kparb_common kparb_logging kparb_arbitrage kparb_ingestion
    PRIVATE nlohmann_json::nlohmann_json
)
