add_library(kparb_logging STATIC
    BinaryLogger.cpp
    BinaryLogReader.cpp
    RecordFormatting.cpp
)
target_link_libraries(kparb_logging PUBLIC kparb_common)
