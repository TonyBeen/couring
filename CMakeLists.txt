cmake_minimum_required(VERSION 3.16)

project(couring VERSION 0.1.0 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure 64-bit file offsets and GNU source features
add_definitions(-D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE)

# Submodule fcontext
set(FCONTEXT_BUILD_TESTS OFF CACHE BOOL "Disable fcontext submodule tests" FORCE)
set(FCONTEXT_BUILD_EXAMPLES OFF CACHE BOOL "Disable fcontext submodule examples" FORCE)
add_subdirectory(3rd/eular/fcontext)

# Submodule liburing
set(LIBURING_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/liburing)
set(LIBURING_STATIC_LIB ${LIBURING_DIR}/src/liburing.a)

add_custom_command(
    OUTPUT ${LIBURING_STATIC_LIB}
    COMMAND bash -c "if [ ! -f config-host.h ]; then ./configure --prefix=/usr; fi"
    COMMAND $(MAKE) -C src liburing.a
    WORKING_DIRECTORY ${LIBURING_DIR}
    COMMENT "Building submodule liburing static library"
)

add_custom_target(liburing_target DEPENDS ${LIBURING_STATIC_LIB})

add_library(uring_static STATIC IMPORTED GLOBAL)
set_target_properties(uring_static PROPERTIES
    IMPORTED_LOCATION ${LIBURING_STATIC_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${LIBURING_DIR}/src/include"
)
add_dependencies(uring_static liburing_target)

set(COURING_SOURCES
    src/fiber.cpp
    src/scheduler.cpp
    src/uring_service.cpp
    src/uring_operation.cpp
    src/error_code.cpp
    src/logger.cpp
    src/io/co_timer.cpp
    src/io/co_socket.cpp
    src/io/co_file.cpp
    src/buffer/buffer_provide.cpp
    src/buffer/buffer_registration.cpp
    src/inner/uring_op_provide_buffer.cpp
    src/inner/fd_manager.cpp
    src/hook.cpp
)

add_library(couring STATIC ${COURING_SOURCES})

target_include_directories(couring PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd/eular/fcontext/include
    ${LIBURING_DIR}/src/include
    /usr/local/include
)

target_link_libraries(couring PUBLIC
    fcontext
    uring_static
    utils
    pthread
    dl
)

# Test executable
enable_testing()
add_subdirectory(test)

# Examples
add_executable(echo_server examples/echo_server.cpp)
target_link_libraries(echo_server PRIVATE couring)

add_executable(benchmark_concurrency examples/benchmark_concurrency.cpp)
target_link_libraries(benchmark_concurrency PRIVATE couring)

add_executable(buffer_example examples/buffer_example.cpp)
target_link_libraries(buffer_example PRIVATE couring)
