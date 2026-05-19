
add_library(Logger STATIC 
    src/Logger.cpp
    include/Logger.h
)

include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
)
FetchContent_MakeAvailable(spdlog)

target_include_directories(Logger PUBLIC include)
target_link_libraries(Logger PRIVATE spdlog::spdlog)
