cmake_minimum_required(VERSION 3.16)

project(WiFiHeadphones LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries")
set(ENABLE_CJSON_UTILS OFF CACHE BOOL "Enable cJSON utils")
set(ENABLE_CJSON_TEST OFF CACHE BOOL "Enable cJSON test")
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "Position independent code")

add_subdirectory(libs/cjson)

add_subdirectory(linux_app)
add_subdirectory(cli)
