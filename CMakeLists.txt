cmake_minimum_required(VERSION 3.21)
project(kerfnest VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KERFNEST_BUILD_TESTS "Build the kerfnest tests" ${PROJECT_IS_TOP_LEVEL})

include(FetchContent)

# Clipper2 (BSL-1.0). Pinned after 2.0.1 for "Stop CheckSplitOwner recursing forever on cyclic splits".
set(CLIPPER2_UTILS OFF CACHE BOOL "" FORCE)
set(CLIPPER2_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLIPPER2_TESTS OFF CACHE BOOL "" FORCE)
set(CLIPPER2_USINGZ OFF CACHE STRING "" FORCE)
FetchContent_Declare(Clipper2
    GIT_REPOSITORY https://github.com/PolyClipping/Clipper2.git
    GIT_TAG b496b454df168a2280354857c1cd90bd6fa7b4dd
    SOURCE_SUBDIR CPP)
FetchContent_MakeAvailable(Clipper2)

add_library(kerfnest STATIC
    src/Shape.cpp
    src/Nfp.cpp)
target_include_directories(kerfnest
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kerfnest PUBLIC Clipper2)
if(MSVC)
    target_compile_options(kerfnest PRIVATE /W4 /permissive-)
else()
    target_compile_options(kerfnest PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(KERFNEST_BUILD_TESTS)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG 2d0a9359a60c51affe2a9bebb1be1dca47868151) # v2.5.3, MIT
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(doctest)

    enable_testing()
    add_executable(kerfnest_tests
        tests/TestMain.cpp
        tests/UnitsTests.cpp
        tests/ShapeTests.cpp
        tests/NfpTests.cpp)
    target_include_directories(kerfnest_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(kerfnest_tests PRIVATE kerfnest Clipper2 doctest::doctest)
    add_test(NAME kerfnest_tests COMMAND kerfnest_tests)
endif()
