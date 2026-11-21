cmake_minimum_required(VERSION 3.25)
project(PRISM VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
option(PRISM_SANITIZE "Enable memory and undefined-behavior checks" OFF)

add_library(prism_options INTERFACE)
target_compile_features(prism_options INTERFACE cxx_std_23)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(prism_options INTERFACE /W4 /WX /permissive-)
else()
    target_compile_options(prism_options INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()
if(PRISM_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "The sanitized preset requires a compatible compiler")
    endif()
    target_compile_options(prism_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(prism_options INTERFACE -fsanitize=address,undefined)
endif()

add_library(prism_math INTERFACE)
target_link_libraries(prism_math INTERFACE prism_options)
target_include_directories(prism_math INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(NOT MSVC)
    target_compile_options(prism_math INTERFACE -ffp-contract=off)
endif()

add_executable(prism_check_comments tools/lint/CheckNoComments.cpp)
target_link_libraries(prism_check_comments PRIVATE prism_options)
add_custom_target(lint
    COMMAND prism_check_comments "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS prism_check_comments
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
