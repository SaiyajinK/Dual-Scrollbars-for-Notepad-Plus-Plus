cmake_minimum_required(VERSION 3.20)
project(DualScrollbars LANGUAGES CXX RC)

add_library(DualScrollbars SHARED
    src/DualScrollbars.cpp
    src/DualScrollbarsVersion.rc
)
target_compile_features(DualScrollbars PRIVATE cxx_std_17)
target_compile_definitions(DualScrollbars PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(DualScrollbars PRIVATE comctl32 uxtheme user32)
set_target_properties(DualScrollbars PROPERTIES PREFIX "" OUTPUT_NAME "DualScrollbars")
