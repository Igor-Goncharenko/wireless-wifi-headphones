project(wifi_headphones_cli LANGUAGES C)

file(GLOB SRC_FILES "src/*.c")

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_compile_options(${PROJECT_NAME}
    PRIVATE
        -Wall
        -g
        ${LIBPULSE_CFLAGS_OTHER}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        pthread
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        cjson
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        include
        ${CMAKE_SOURCE_DIR}/libs/cjson
)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
