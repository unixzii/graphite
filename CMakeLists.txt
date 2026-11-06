cmake_minimum_required(VERSION 3.20)
project(graphite VERSION 0.0.1 LANGUAGES C)

include(CTest)
option(GRAPHITE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
add_subdirectory(src)
