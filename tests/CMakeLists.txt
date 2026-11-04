include(GoogleTest)

file(GLOB_RECURSE KPARB_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*Test.cpp)
list(APPEND KPARB_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/AllocationCounter.cpp)

add_executable(kparb_tests ${KPARB_TEST_SOURCES})
target_link_libraries(kparb_tests PRIVATE kparb_build_options GTest::gtest_main OpenSSL::Crypto nlohmann_json::nlohmann_json)

foreach(library kparb_common kparb_logging kparb_arbitrage kparb_ingestion kparb_matching kparb_application)
    if(TARGET ${library})
        target_link_libraries(kparb_tests PRIVATE ${library})
    endif()
endforeach()

target_include_directories(kparb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(kparb_tests PRIVATE KPARB_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

gtest_discover_tests(kparb_tests DISCOVERY_TIMEOUT 60)
