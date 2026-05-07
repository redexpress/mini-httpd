cmake_minimum_required(VERSION 3.14)
project(minihttpd)

set(CMAKE_CXX_STANDARD 17)

add_executable(minihttpd main.c handler.c)
