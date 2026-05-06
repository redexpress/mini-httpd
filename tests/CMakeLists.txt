cmake_minimum_required(VERSION 3.14)
project(minihttpd_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

include(FetchContent)
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0
    SOURCE_SUBDIR extras
)
FetchContent_MakeAvailable(catch2)

enable_testing()

add_executable(tests test.cpp ${catch2_SOURCE_DIR}/extras/catch_amalgamated.cpp)
target_include_directories(tests PRIVATE ${catch2_SOURCE_DIR}/extras)
