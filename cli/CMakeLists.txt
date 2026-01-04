project(wifi_headphones_cli LANGUAGES C)

file(GLOB SRC_FILES "src/*.c")

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_compile_options(${PROJECT_NAME}
    PRIVATE
        -Wall
        -g
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        pthread
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        cjson
        wh_daemon_commands
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        include
        ${CMAKE_SOURCE_DIR}/libs/cjson
        ${CMAKE_SOURCE_DIR}/libs/wh_daemon_commands
)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
