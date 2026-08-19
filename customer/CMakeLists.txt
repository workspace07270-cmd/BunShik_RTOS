add_library(bunshik_customer_core
    src/http_client.c
    src/print_job.c
    src/printer.c
)

target_include_directories(bunshik_customer_core PUBLIC include)
target_link_libraries(bunshik_customer_core PUBLIC Threads::Threads)

add_executable(bunshik_customer src/main.c)
target_link_libraries(bunshik_customer PRIVATE bunshik_customer_core)