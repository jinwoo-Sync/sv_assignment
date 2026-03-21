add_executable(test_logger test_logger.cpp)
target_link_libraries(test_logger PRIVATE sv_logger GTest::gtest_main Threads::Threads)
gtest_discover_tests(test_logger)

add_executable(test_memory_pool test_memory_pool.cpp)
target_link_libraries(test_memory_pool PRIVATE sv_core GTest::gtest_main Threads::Threads)
gtest_discover_tests(test_memory_pool)
