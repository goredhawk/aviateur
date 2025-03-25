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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# To fix "error: call to undeclared library function 'memset' with type 'void *(void *, int, unsigned long)'".
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_C_STANDARD 90)
    set(CMAKE_C_STANDARD_REQUIRED TRUE)
endif ()

find_package(PkgConfig REQUIRED)

if (WIN32)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

    find_package(FFmpeg REQUIRED)
    find_package(unofficial-sodium CONFIG REQUIRED)
    find_package(OpenCV REQUIRED)
else ()
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
            libavformat
            libavcodec
            libswresample
            libswscale
            libavutil
    )
    pkg_check_modules(LIBSODIUM REQUIRED IMPORTED_TARGET libsodium)
    find_package(OpenCV REQUIRED)
endif ()

file(GLOB_RECURSE SRC_LIST
        src/*.cpp
        src/*.c
        src/*.h
)

file(COPY gs.key assets DESTINATION ${CMAKE_BINARY_DIR}/bin)

add_executable(${PROJECT_NAME} ${SRC_LIST})

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

if (WIN32)
    target_link_libraries(${PROJECT_NAME}
            ${FFMPEG_LIBRARIES}
            PkgConfig::LIBUSB
            unofficial-sodium::sodium
            ${OpenCV_LIBS}
            WiFiDriver
            flint_gui
    )
else ()
    target_link_libraries(${PROJECT_NAME}
            PkgConfig::LIBAV
            PkgConfig::LIBSODIUM
            ${OpenCV_LIBS}
            WiFiDriver
            flint_gui
    )
endif ()
