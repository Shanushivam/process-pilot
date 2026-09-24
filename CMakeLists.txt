cmake_minimum_required(VERSION 3.10)

project(ProcessPilot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Threads REQUIRED)

add_library(processpilot_core STATIC
    src/daemon/daemon.cpp
    src/process/process_manager.cpp
    src/config/config_parser.cpp
    src/service/service_config.cpp
    src/monitor/resource_monitor.cpp
    src/dependency/dependency_graph.cpp
    src/ipc/unix_socket.cpp
    src/logging/logger.cpp
)
target_include_directories(processpilot_core PUBLIC src)
target_compile_options(processpilot_core PUBLIC -Wall -Wextra -Wpedantic)

add_executable(processpilot src/main.cpp)
target_link_libraries(processpilot PRIVATE processpilot_core)

add_executable(processpilot-cli cli/processpilot_cli.cpp)
target_link_libraries(processpilot-cli PRIVATE processpilot_core)

enable_testing()

foreach(test_name process_tests config_tests dependency_tests daemon_tests ipc_tests)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE processpilot_core Threads::Threads)
    target_compile_definitions(${test_name} PRIVATE
        PROCESSPILOT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 60)
endforeach()
