
add_library(SignalFileIO STATIC 
    src/SignalFileIO.cpp
    include/SignalFileIO.h
)




target_include_directories(SignalFileIO PUBLIC include)
target_link_libraries(SignalFileIO PRIVATE Logger )