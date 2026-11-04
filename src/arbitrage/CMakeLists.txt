add_library(kparb_arbitrage STATIC
    ArbitrageEngine.cpp
    FeeSchedule.cpp
)
target_link_libraries(kparb_arbitrage PUBLIC kparb_common kparb_logging)
