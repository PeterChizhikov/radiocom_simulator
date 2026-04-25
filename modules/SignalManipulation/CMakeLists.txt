find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED fftw3)


add_library(SignalManipulation STATIC 
    src/SignalManipulation.cpp
    include/SignalManipulation.h
    include/Hamming.h
)




target_include_directories(SignalManipulation PUBLIC include ${FFTW3_INCLUDE_DIRS})
target_link_libraries(SignalManipulation PRIVATE ${FFTW3_LIBRARIES})