cmake_minimum_required(VERSION 3.16)

project(linux_native_hook_v1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(linux_native_hook_common
    common/socket_fd.cpp
)

set_target_properties(linux_native_hook_common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(linux_native_hook_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(linux_native_hook_common
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(consumer
    consumer/server_main.cpp
    consumer/control_server.cpp
    consumer/shm_consumer.cpp
    consumer/metrics.cpp
)

target_link_libraries(consumer
    PRIVATE
        linux_native_hook_common
        Threads::Threads
        rt
)

target_compile_options(consumer
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(producer_fake
    producer_fake/fake_client_main.cpp
    producer_fake/fake_writer.cpp
)

target_link_libraries(producer_fake
    PRIVATE
        linux_native_hook_common
        Threads::Threads
        rt
)

target_compile_options(producer_fake
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(perf_test_data_linux
    tests/perf_test_data_linux.cpp
)

target_compile_options(perf_test_data_linux
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(perf_test_data_linux
    PRIVATE
        Threads::Threads
)

add_executable(ablation_config_test
    tests/ablation_config_test.cpp
    producer_hook/ablation_config.cpp
)

target_include_directories(ablation_config_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(ablation_config_test
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

enable_testing()
add_test(NAME ablation_config_pid_tid_cache_default COMMAND ablation_config_test pid_tid_cache 0)
add_test(NAME ablation_config_pid_tid_cache_disabled COMMAND ablation_config_test pid_tid_cache 0)
set_tests_properties(ablation_config_pid_tid_cache_disabled PROPERTIES
    ENVIRONMENT "LNHV1_PID_TID_CACHE=0")
add_test(NAME ablation_config_pid_tid_cache_enabled COMMAND ablation_config_test pid_tid_cache 1)
set_tests_properties(ablation_config_pid_tid_cache_enabled PROPERTIES
    ENVIRONMENT "LNHV1_PID_TID_CACHE=1")
add_test(NAME ablation_config_pid_tid_cache_invalid COMMAND ablation_config_test pid_tid_cache 0)
set_tests_properties(ablation_config_pid_tid_cache_invalid PROPERTIES
    ENVIRONMENT "LNHV1_PID_TID_CACHE=invalid")
add_test(NAME ablation_config_sub_ablation_stage_21 COMMAND ablation_config_test sub_ablation 21)
set_tests_properties(ablation_config_sub_ablation_stage_21 PROPERTIES
    ENVIRONMENT "LNHV1_SUBABLATION_STAGE=21")

add_library(hook_preload SHARED
    producer_hook/ablation_config.cpp
    producer_hook/hook_preload.cpp
    producer_hook/hook_guard.cpp
    producer_hook/hook_writer.cpp
)

target_link_libraries(hook_preload
    PRIVATE
        linux_native_hook_common
        Threads::Threads
        dl
        rt
)

target_compile_options(hook_preload
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_include_directories(hook_preload
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(hook_preload PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hook_preload"
)
