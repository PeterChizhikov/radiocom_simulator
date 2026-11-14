add_library(Link STATIC Link.cpp LinkPacket.cpp)
target_include_directories(Link PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Link PUBLIC Sdr SignalManipulation Logger PRIVATE ProjectWarnings)
