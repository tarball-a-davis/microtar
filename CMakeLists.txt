cmake_minimum_required(VERSION 3.16)
project(microtar)

option(MTAR_ENABLE_TESTS "" ON)

IF (MTAR_ENABLE_TESTS)
    enable_testing()

    set(CMAKE_CXX_STANDARD 14)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    include(FetchContent)
    cmake_policy(SET CMP0135 OLD)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)
ENDIF(MTAR_ENABLE_TESTS)

# microtar
add_compile_options(-Wall -Werror -pedantic)
add_subdirectory(src)
