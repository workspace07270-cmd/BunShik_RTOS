add_library(bunshik_admin_core
    src/backend_client.c
    src/http_client.c
    src/order.c
    src/scheduler.c
)

target_include_directories(bunshik_admin_core PUBLIC include)
target_link_libraries(bunshik_admin_core PUBLIC Threads::Threads)

add_executable(bunshik_admin src/main.c)
target_link_libraries(bunshik_admin PRIVATE bunshik_admin_core)
