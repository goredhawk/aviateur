cmake_minimum_required(VERSION 3.20)

# This should go before project declaration.
if (WIN32)
    message("[aviateur] <${VCPKG_ROOT}> should be the vcpkg dir in which you have installed the dependency libs.")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif ()

project(aviateur
        VERSION 0.1
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(PkgConfig REQUIRED)

find_package(FFmpeg REQUIRED)
include_directories(${FFMPEG_INCLUDE_DIRS})

find_package(unofficial-sodium CONFIG REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
find_package(OpenCV REQUIRED)

file(GLOB_RECURSE SRC_LIST
        src/*.cpp
        src/*.c
        src/*.h
)

file(COPY gs.key assets DESTINATION ${CMAKE_BINARY_DIR})

add_executable(${PROJECT_NAME} ${SRC_LIST}
        src/gui/settings_tab.cpp
        src/gui/settings_tab.h)

if (WIN32)
    string(APPEND CMAKE_CXX_FLAGS " /utf-8")
endif ()

add_subdirectory(3rd/devourer)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/devourer/src" "3rd/devourer/hal")

add_subdirectory(3rd/flint_gui)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/flint_gui/src")

add_subdirectory(3rd/json)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/json/include")

add_subdirectory(3rd/mINI)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/mINI/src")

target_link_libraries(${PROJECT_NAME}
        ${FFMPEG_LIBRARIES}
        PkgConfig::LIBUSB
        unofficial-sodium::sodium
        ${OpenCV_LIBS}
        WiFiDriver
        flint_gui
)
