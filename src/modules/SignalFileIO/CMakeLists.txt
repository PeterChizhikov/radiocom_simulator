add_library(SignalFileIO STATIC
    SignalFileIO.cpp
)
target_include_directories(SignalFileIO PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SignalFileIO
    PUBLIC Common Logger
    PRIVATE ProjectWarnings
)
