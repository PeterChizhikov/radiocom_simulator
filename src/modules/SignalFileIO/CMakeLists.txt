
add_library(SignalFileIO STATIC 
    src/SignalFileIO.cpp
    include/SignalFileIO.h
)




target_include_directories(SignalFileIO PUBLIC include ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(SignalFileIO PRIVATE Logger )