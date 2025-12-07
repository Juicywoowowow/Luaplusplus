cmake_minimum_required(VERSION 3.14)
project(luapp_tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Lua++ source files (compile as C)
set(LUAPP_SOURCES
    ../src/chunk.c
    ../src/compiler.c
    ../src/debug.c
    ../src/lexer.c
    ../src/memory.c
    ../src/object.c
    ../src/table.c
    ../src/value.c
    ../src/vm.c
)

# Create a library from Lua++ sources (excluding main.c)
add_library(luapp_lib ${LUAPP_SOURCES})
target_include_directories(luapp_lib PUBLIC ../src)
set_target_properties(luapp_lib PROPERTIES LINKER_LANGUAGE C)

# Need to define debugFlags in tests since main.c isn't included
add_library(luapp_test_main test_main.cpp)
target_link_libraries(luapp_test_main luapp_lib)

# Test executable
add_executable(luapp_tests
    test_lexer.cpp
    test_compiler.cpp
    test_value.cpp
    test_vm.cpp
    test_oop.cpp
)

target_link_libraries(luapp_tests
    luapp_test_main
    luapp_lib
    GTest::gtest
    GTest::gtest_main
)

# Enable testing
enable_testing()
include(GoogleTest)
gtest_discover_tests(luapp_tests)
