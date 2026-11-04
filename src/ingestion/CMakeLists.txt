add_library(kparb_ingestion STATIC
    HttpClient.cpp
    JsonDocumentBuffer.cpp
    KalshiBookFeed.cpp
    KalshiMarketCatalog.cpp
    KalshiMessageParser.cpp
    KalshiSigner.cpp
    LadderDirectory.cpp
    PolymarketBookFeed.cpp
    PolymarketMarketCatalog.cpp
    PolymarketMessageParser.cpp
    UpdatePublisher.cpp
    UrlEncoding.cpp
    WebSocketBookFeed.cpp
)
target_link_libraries(kparb_ingestion
    PUBLIC kparb_common kparb_logging kparb_arbitrage simdjson::simdjson
    PRIVATE nlohmann_json::nlohmann_json ixwebsocket::ixwebsocket OpenSSL::Crypto
)
