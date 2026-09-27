find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)

add_library(SignalManipulation STATIC
    SignalManipulation.cpp
)
target_include_directories(SignalManipulation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SignalManipulation
    PUBLIC Common Logger SignalFileIO
    PRIVATE PkgConfig::FFTW3 ProjectWarnings
)
