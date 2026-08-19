add_executable(admin_scheduler_test admin_scheduler_test.c)
target_link_libraries(admin_scheduler_test PRIVATE bunshik_admin_core)

add_test(NAME admin_scheduler_test COMMAND admin_scheduler_test)
