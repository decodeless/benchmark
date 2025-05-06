# Copyright (c) 2024 Pyarelal Knowles, MIT License

cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)
project(decodeless_benchmark)
include(FetchContent)

option(DECODELESS_SEARCH_DEPENDENCIES "Enable searching for dependencies in adjacent directories" OFF)
option(DECODELESS_FETCH_DEPENDENCIES "Enable fetching dependencies with cmake FetchContent" ON)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
    nanobench
    GIT_REPOSITORY https://github.com/martinus/nanobench.git
    GIT_TAG v4.1.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nanobench)

FetchContent_Declare(
    decodeless_offset_ptr
    GIT_REPOSITORY https://github.com/decodeless/offset_ptr.git
    GIT_TAG 38ceefc6ce63fb4667cd207424b1277c3eed5f8d
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(decodeless_offset_ptr)

FetchContent_Declare(
    decodeless_writer
    GIT_REPOSITORY https://github.com/decodeless/writer.git
    GIT_TAG 2afa7104b986e52031b0969f30512d7995623a84
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(decodeless_writer)

enable_testing()

add_executable(decodeless_benchmark
    benchmark.cpp
    )

target_link_libraries(decodeless_benchmark PRIVATE
    decodeless::writer
    decodeless::offset_ptr
    gtest_main
    nanobench
    )

if(MSVC)
  target_compile_options(decodeless_benchmark PRIVATE /W4 /WX)
  target_compile_definitions(decodeless_benchmark PRIVATE WIN32_LEAN_AND_MEAN=1
                                                           NOMINMAX)
else()
  target_compile_options(decodeless_benchmark PRIVATE -Wall -Wextra -Wpedantic
                                                       -Werror)
endif()

#target_compile_options(decodeless_benchmark PRIVATE -g -Og)
#set(CMAKE_VERBOSE_MAKEFILE ON)

include(GoogleTest)
gtest_discover_tests(decodeless_benchmark)
