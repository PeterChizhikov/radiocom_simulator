find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET GLOBAL fftw3)

# Турбокод LTE из AFF3CT (MIT). Версия AFF3CT берётся из тегов git, которых нет в клоне FetchContent, поэтому задана явно
include(FetchContent)
set(AFF3CT_COMPILE_EXE OFF CACHE BOOL "" FORCE)
set(AFF3CT_COMPILE_STATIC_LIB ON CACHE BOOL "" FORCE)
set(AFF3CT_COMPILE_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(AFF3CT_OVERRIDE_VERSION v0.0.0 CACHE STRING "" FORCE)
set(SPU_TESTS OFF CACHE BOOL "" FORCE)
set(SPU_BENCHMARK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    aff3ct
    GIT_REPOSITORY https://github.com/aff3ct/aff3ct.git
    GIT_TAG 406cb659cb4658ce554147e5fe995bbd89569a04
)
FetchContent_MakeAvailable(aff3ct)
# Заголовки AFF3CT не проверяются нашими предупреждениями
set_target_properties(aff3ct-static-lib PROPERTIES SYSTEM TRUE)

add_library(SignalManipulation STATIC
    SignalManipulation.cpp
)
target_include_directories(SignalManipulation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SignalManipulation
    PUBLIC Common Logger
    PRIVATE PkgConfig::FFTW3 aff3ct-static-lib ProjectWarnings
)
