add_library(SignalManipulation STATIC 
    src/SignalManipulation.cpp
    include/SignalManipulation.h
)

target_include_directories(SignalManipulation PUBLIC include)