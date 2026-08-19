cmake_minimum_required(VERSION 3.20)

project(BunShikRTOS VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)

enable_testing()
add_subdirectory(admin)
add_subdirectory(customer)
add_subdirectory(tests)
