cmake_minimum_required(VERSION 3.16)

project(WiFiHeadphones LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_subdirectory(linux_app)
add_subdirectory(cli)
