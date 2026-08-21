cmake_minimum_required(VERSION 3.20)

project(BunShikRTOS VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)

# --- FreeRTOS-Kernel (src/customer_main.c가 사용) ---
if(DEFINED ENV{FREERTOS_KERNEL_PATH})
    set(FREERTOS_KERNEL_PATH "$ENV{FREERTOS_KERNEL_PATH}")
elseif(EXISTS "$ENV{HOME}/rtos-kiosk-course/third_party/FreeRTOS-Kernel/CMakeLists.txt")
    set(FREERTOS_KERNEL_PATH "$ENV{HOME}/rtos-kiosk-course/third_party/FreeRTOS-Kernel")
elseif(EXISTS "$ENV{HOME}/FreeRTOS-Kernel/CMakeLists.txt")
    set(FREERTOS_KERNEL_PATH "$ENV{HOME}/FreeRTOS-Kernel")
else()
    message(FATAL_ERROR "FreeRTOS-Kernel not found. Set FREERTOS_KERNEL_PATH.")
endif()

message(STATUS "FreeRTOS Kernel: ${FREERTOS_KERNEL_PATH}")
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/config")
target_compile_definitions(freertos_config INTERFACE projCOVERAGE_TEST=0)
set(FREERTOS_PORT "GCC_POSIX" CACHE STRING "" FORCE)
set(FREERTOS_HEAP "4" CACHE STRING "" FORCE)
# FreeRTOS의 POSIX 포트(GCC_POSIX)는 CLOCK_REALTIME, PTHREAD_MUTEX_ROBUST 같은
# GNU/POSIX 확장에 의존합니다. 프로젝트 전체에는 CMAKE_C_EXTENSIONS OFF를 쓰고
# 있으므로, FreeRTOS-Kernel을 추가하는 동안만 잠시 켰다가 다시 끕니다.
set(CMAKE_C_EXTENSIONS ON)
add_subdirectory("${FREERTOS_KERNEL_PATH}" freertos-kernel)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_library(bunshik_core
    src/admin_catalog_monitor.c
    src/admin_logger.c
    src/admin_service.c
    src/backend_client.c
    src/customer_http_client.c
    src/customer_main.c
    src/http_client.c
    src/order_priority.c
    src/print_job.c
    src/printer.c
    src/printer_paper.c
    src/rtos_runtime.c
    src/system_monitor.c
)
target_include_directories(bunshik_core PUBLIC include)
target_link_libraries(bunshik_core PUBLIC freertos_kernel freertos_config)

add_executable(bunshik_rtos src/main.c)
target_link_libraries(bunshik_rtos PRIVATE bunshik_core)

set(BUNSHIK_API_BASE_URL "http://127.0.0.1:8080" CACHE STRING
    "Spring Boot backend URL used by the run target")
add_custom_target(run
    COMMAND ${CMAKE_COMMAND} -E env
        "BUNSHIK_API_BASE_URL=${BUNSHIK_API_BASE_URL}"
        $<TARGET_FILE:bunshik_rtos>
    DEPENDS bunshik_rtos
    USES_TERMINAL
    COMMENT "Running BunShik RTOS (${BUNSHIK_API_BASE_URL})"
)

add_subdirectory(tests)
