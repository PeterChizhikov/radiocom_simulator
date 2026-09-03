cmake_minimum_required(VERSION 3.10)

project(Radiocom_simulator)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMPILE_COMMANDS_LINK "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json")
set(COMPILE_COMMANDS_FILE "${CMAKE_BINARY_DIR}/compile_commands.json")
if(UNIX)
    if(EXISTS "${COMPILE_COMMANDS_LINK}")
        if(NOT IS_SYMLINK "${COMPILE_COMMANDS_LINK}")
            file(REMOVE "${COMPILE_COMMANDS_LINK}")
        endif()
    endif()
    if(NOT EXISTS "${COMPILE_COMMANDS_LINK}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E create_symlink "${COMPILE_COMMANDS_FILE}" "${COMPILE_COMMANDS_LINK}"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endif()
endif()

# find_package(PostgreSQL REQUIRED)

set(HEADER_FILES
    include/SimulationCore.h
    include/Types.h
#     include/Calculator.h
#     include/Checker.h
#     include/Parser.h 
#     include/Printer.h
#     include/Runner.h
#     include/MathInfo.h
#     include/DatabaseProcessor.h
#     include/CalculatorException.h
# 
)

set(SOURCE_FILES 
    src/main.cpp
    src/SimulationCore.cpp
    # src/Calculator.cpp
    # src/Checker.cpp
    # src/Parser.cpp
    # src/Printer.cpp
    # src/Runner.cpp
    # src/DatabaseProcessor.cpp
    # src/CalculatorException.cpp
)

# set(TEST_SOURCE_FILES
#     tests/CalculatorTests.cpp
# )

# set(TEST_APP_SOURCE_FILES
#     src/Calculator.cpp
#     src/Checker.cpp
#     src/Logger.cpp
#     src/Parser.cpp
#     src/Printer.cpp
#     src/Runner.cpp
#     src/DatabaseProcessor.cpp
#     src/CalculatorException.cpp
# )

include(FetchContent)

# FetchContent_Declare(
#     mathLib
#     GIT_REPOSITORY https://github.com/PeterChizhikov/mathLib.git
#     GIT_TAG main
# )

# FetchContent_Declare(
#     json 
#     GIT_REPOSITORY https://github.com/nlohmann/json.git
#     GIT_TAG v3.11.3
# )

# FetchContent_Declare(
#     googletest 
#     GIT_REPOSITORY https://github.com/google/googletest.git
#     GIT_TAG v1.15.2
# )



# FetchContent_MakeAvailable(mathLib)
# FetchContent_MakeAvailable(json)

# add_subdirectory(cache)
add_subdirectory(src/modules/SignalManipulation)
add_subdirectory(src/modules/Logger)
add_subdirectory(src/modules/SignalFileIO)

add_executable(radiocom_sim ${SOURCE_FILES})

target_include_directories(radiocom_sim PRIVATE 
include
${FFTW3_INCLUDE_DIRS})
target_link_directories(radiocom_sim PRIVATE ${FFTW3_LIBRARY_DIRS})
target_link_libraries(radiocom_sim
    PRIVATE
    SignalManipulation
    Logger
    SignalFileIO
    ${FFTW3_LIBRARIES}
#     mathLib
#     nlohmann_json::nlohmann_json
#     PostgreSQL::PostgreSQL
#     cache_lib
)

install(TARGETS radiocom_sim RUNTIME DESTINATION bin)


# find_program(CLANG_FORMAT NAMES clang-format clang-format-18 clang-format-17 clang-format-16)
# if(CLANG_FORMAT)
#     add_custom_target(format
#         COMMAND ${CLANG_FORMAT} -i ${SOURCE_FILES} ${HEADER_FILES} ${TEST_SOURCE_FILES}
#         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
#         COMMENT "Formatting sources with clang-format"
#     )
# else()
#     message(WARNING "clang-format not found. Target 'format' will not be available.")
# endif()

# find_program(CLANG_TIDY NAMES clang-tidy clang-tidy-18 clang-tidy-17 clang-tidy-16)
# if(CLANG_TIDY)
#     set(TIDY_SOURCES "")
#     foreach(F ${SOURCE_FILES})
#         list(APPEND TIDY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${F})
#     endforeach()
#     add_custom_target(tidy
#         COMMAND ${CLANG_TIDY} ${TIDY_SOURCES}
#         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
#         COMMENT "Running clang-tidy on sources"
#     )
# else()
#     message(WARNING "clang-tidy not found. Target 'tidy' will not be available.")
# endif()

# if(CLANG_FORMAT AND CLANG_TIDY)
#     add_dependencies(radiocom_sim format tidy)
#     # if(BUILD_TESTING)
#     #     add_dependencies(calc_tests format)
#     # endif()
# endif()