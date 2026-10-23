add_executable(couring_tests
    test_main.cpp
    test_fiber.cpp
    test_scheduler.cpp
    test_timer.cpp
    test_io.cpp
    test_hook.cpp
    test_logger.cpp
    test_buffer.cpp
    test_sync.cpp
)

target_include_directories(couring_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(couring_tests PRIVATE
    couring
)

add_test(NAME AllCouringTests COMMAND couring_tests)
