include(FetchContent)

set(FETCHCONTENT_QUIET ON)

if(APPLE AND NOT OPENSSL_ROOT_DIR)
    if(EXISTS "/opt/homebrew/opt/openssl@3")
        set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@3")
    elseif(EXISTS "/usr/local/opt/openssl@3")
        set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl@3")
    endif()
endif()

find_package(OpenSSL 3 REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    simdjson
    URL https://github.com/simdjson/simdjson/archive/refs/tags/v3.10.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)
set(SIMDJSON_DEVELOPER_MODE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    ixwebsocket
    URL https://github.com/machinezone/IXWebSocket/archive/refs/tags/v11.4.5.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)
set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
set(USE_ZLIB ON CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(simdjson nlohmann_json ixwebsocket)

if(KPARB_BUILD_TESTS)
    FetchContent_MakeAvailable(googletest)
endif()
