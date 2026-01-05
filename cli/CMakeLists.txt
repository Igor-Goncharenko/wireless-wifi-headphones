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
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        include
        ${CMAKE_SOURCE_DIR}/libs/cjson
        ${CMAKE_SOURCE_DIR}/libs/protocols
)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
