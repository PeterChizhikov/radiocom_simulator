# libiio на macOS ставится как framework, pkg-config для него ненадёжен.
# Homebrew на Apple Silicon проверяется первым: framework из /Library/Frameworks может быть только x86_64.
# Заголовки берутся из того же framework, что и библиотека, чтобы версии совпадали
find_library(IIO_LIBRARY iio HINTS /opt/homebrew/Frameworks)
set(CMAKE_FIND_FRAMEWORK NEVER)
find_path(IIO_INCLUDE_DIR iio.h HINTS ${IIO_LIBRARY}/Headers)
if(NOT IIO_INCLUDE_DIR OR NOT IIO_LIBRARY)
    message(FATAL_ERROR "libiio not found: install it (brew install libiio / apt install libiio-dev)")
endif()

add_library(Sdr STATIC
    Sdr.cpp
)
target_include_directories(Sdr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(Sdr SYSTEM PRIVATE ${IIO_INCLUDE_DIR})
target_link_libraries(Sdr
    PUBLIC Common Logger
    PRIVATE ${IIO_LIBRARY} ProjectWarnings
)
