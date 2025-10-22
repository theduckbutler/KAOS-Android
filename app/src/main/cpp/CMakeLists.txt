cmake_minimum_required(VERSION 3.18.1)

project("portal_emulator")

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the native library
add_library(portal_emulator
    SHARED
    portal_emulator.cpp
)

add_executable(portal_daemon
        portal_daemon.cpp
        skylander_crypto.c
        rijndael.c
)

# Include directories
target_include_directories(
    portal_emulator
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link libraries
target_link_libraries(portal_emulator
    android
    log
)

target_link_libraries(portal_daemon
        log
)

# Compiler flags
target_compile_options(
    portal_emulator
    PRIVATE
    -Wall
    -Wextra
    -O2
)

add_custom_command(TARGET portal_daemon POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:portal_daemon>
        ${CMAKE_CURRENT_SOURCE_DIR}/../assets/${ANDROID_ABI}/
)

set_target_properties(portal_daemon PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../assets/${ANDROID_ABI}"
)

# Create the assets directory if it doesn't exist
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../assets/${ANDROID_ABI}")