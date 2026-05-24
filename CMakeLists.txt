cmake_minimum_required(VERSION 3.19)
project(hermes-hyprland VERSION 1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED hyprland)

add_library(hermes-hyprland SHARED main.cpp)

target_include_directories(hermes-hyprland PRIVATE
    /usr/include
    /usr/include/pixman-1
    /usr/include/hyprland
    /usr/include/hyprland/src
    /usr/include/hyprland/protocols
    /usr/include/libdrm
    /usr/include/drm
)

target_compile_options(hermes-hyprland PRIVATE
    ${HYPRLAND_CFLAGS_OTHER}
    -DWLR_USE_UNSTABLE
)

target_link_libraries(hermes-hyprland PRIVATE
    ${HYPRLAND_LIBRARIES}
)

set_target_properties(hermes-hyprland PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)

