project(wh_daemon_commands LANGUAGES C)

set(SRC "wh_daemon_commands.c")
set(HDR "wh_daemon_commands.h")

add_library(${PROJECT_NAME} STATIC ${SRC} ${HDR})

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        cjson
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/cjson
)
