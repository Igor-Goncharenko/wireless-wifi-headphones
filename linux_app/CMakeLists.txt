project(wifi_headphones_daemon LANGUAGES C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPULSE REQUIRED libpulse)

add_compile_definitions(CONFIG_DAEMON_NAME="${PROJECT_NAME}")

file(GLOB SRC_FILES "src/*.c")

add_executable(${PROJECT_NAME} ${SRC_FILES})

#target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-Bdynamic")

target_compile_options(${PROJECT_NAME}
    PRIVATE
        -Wall
        -g
        ${LIBPULSE_CFLAGS_OTHER}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${LIBPULSE_LINK_LIBRARIES}
        pthread
        m
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        cjson
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        include
        ${LIBPULSE_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/libs/cjson
        ${CMAKE_SOURCE_DIR}/libs/wh_daemon_commands
)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
