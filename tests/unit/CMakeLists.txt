add_executable(test_logger test_logger.cpp)
target_link_libraries(test_logger PRIVATE sv_logger GTest::gtest_main Threads::Threads)
gtest_discover_tests(test_logger)

add_executable(test_memory_pool test_memory_pool.cpp)
target_link_libraries(test_memory_pool PRIVATE sv_core GTest::gtest_main Threads::Threads)
gtest_discover_tests(test_memory_pool)

add_executable(test_policy_engine test_policy_engine.cpp)
target_link_libraries(test_policy_engine PRIVATE sv_policy GTest::gtest_main Threads::Threads)
gtest_discover_tests(test_policy_engine)

add_executable(test_cmd_set_mode_ack test_cmd_set_mode_ack.cpp)
target_link_libraries(test_cmd_set_mode_ack PRIVATE sv_core GTest::gtest_main Threads::Threads)
gtest_discover_tests(test_cmd_set_mode_ack)
