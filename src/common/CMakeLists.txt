add_library(kparb_common STATIC
    EnvironmentFile.cpp
    TimeParsing.cpp
)
target_include_directories(kparb_common PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(kparb_common PUBLIC kparb_build_options Threads::Threads)
