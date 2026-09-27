cmake_minimum_required(VERSION 3.16)

project(RadiocomSimulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMPILE_COMMANDS_LINK "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json")
set(COMPILE_COMMANDS_FILE "${CMAKE_BINARY_DIR}/compile_commands.json")
if(UNIX)
    if(EXISTS "${COMPILE_COMMANDS_LINK}" AND NOT IS_SYMLINK "${COMPILE_COMMANDS_LINK}")
        file(REMOVE "${COMPILE_COMMANDS_LINK}")
    endif()
    if(NOT EXISTS "${COMPILE_COMMANDS_LINK}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E create_symlink "${COMPILE_COMMANDS_FILE}" "${COMPILE_COMMANDS_LINK}"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endif()
endif()

option(RADIOCOM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(ProjectWarnings INTERFACE)
if(MSVC)
    target_compile_options(ProjectWarnings INTERFACE /W4 $<$<BOOL:${RADIOCOM_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(ProjectWarnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        $<$<BOOL:${RADIOCOM_WARNINGS_AS_ERRORS}>:-Werror>
    )
endif()

add_subdirectory(src/modules/Common)
add_subdirectory(src/modules/Logger)
add_subdirectory(src/modules/SignalFileIO)
add_subdirectory(src/modules/SignalManipulation)

add_library(SimulationCore STATIC
    src/SimulationCore.cpp
)
target_include_directories(SimulationCore PUBLIC src)
target_link_libraries(SimulationCore
    PUBLIC SignalManipulation Logger
    PRIVATE ProjectWarnings
)

add_executable(radiocom_sim
    src/main.cpp
)
target_link_libraries(radiocom_sim
    PRIVATE SimulationCore SignalManipulation SignalFileIO Logger ProjectWarnings
)

install(TARGETS radiocom_sim RUNTIME DESTINATION bin)
