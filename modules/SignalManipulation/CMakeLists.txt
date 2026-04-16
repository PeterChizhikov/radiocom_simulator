add_library(SignalManipulation STATIC 
    src/SignalManipulation.cpp
    include/SignalManipulation.h
    include/Hamming.h
)

target_include_directories(SignalManipulation PUBLIC include)