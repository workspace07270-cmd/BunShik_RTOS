cmake_minimum_required(VERSION 3.20)

project(BunShikRTOS VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)

enable_testing()

add_library(bunshik_core
    src/admin_logger.c
    src/admin_main.c
    src/backend_client.c
    src/customer_http_client.c
    src/customer_main.c
    src/http_client.c
    src/order.c
    src/order_priority.c
    src/print_job.c
    src/printer.c
    src/scheduler.c
)
target_include_directories(bunshik_core PUBLIC include)
target_link_libraries(bunshik_core PUBLIC Threads::Threads)

add_executable(bunshik_rtos src/main.c)
target_link_libraries(bunshik_rtos PRIVATE bunshik_core)

add_subdirectory(tests)
