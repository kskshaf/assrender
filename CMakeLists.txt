cmake_minimum_required(VERSION 3.12)
project(assrender)

include(GNUInstallDirs)

add_subdirectory(src)

if(MINGW)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -Wl,--add-stdcall-alias")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -Wpedantic")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "-s")
    set(CMAKE_C_FLAGS_RELEASE "-O3")
endif()

# uninstall target
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
