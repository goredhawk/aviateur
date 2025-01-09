cmake_minimum_required(VERSION 3.20)

# This should go before project declaration.
if (WIN32)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif ()

project(aviateur)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(PkgConfig REQUIRED)

find_package(FFmpeg REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
include_directories(${FFMPEG_INCLUDE_DIRS})

file(GLOB_RECURSE SRC_LIST
        src/*.cpp
        src/*.c
        src/*.h
)

file(COPY gs.key openipc-logo-white.svg DESTINATION ${CMAKE_BINARY_DIR})

add_executable(${PROJECT_NAME} ${SRC_LIST})

if (WIN32)
    string(APPEND CMAKE_CXX_FLAGS " /utf-8")
endif ()

add_subdirectory(3rd/devourer)
target_include_directories(${PROJECT_NAME} PRIVATE 3rd/devourer/src 3rd/devourer/hal)

add_subdirectory(3rd/flint_gui)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/flint_gui/src")

add_subdirectory(3rd/json)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/json/include")

target_link_libraries(${PROJECT_NAME}
        ${FFMPEG_LIBRARIES}
        PkgConfig::LIBUSB
        unofficial-sodium::sodium
        WiFiDriver
        flint_gui
)
