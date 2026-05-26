cmake_minimum_required(VERSION 3.16)

project(linux_native_hook_v1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LNHV1_ENABLE_LIBBPF "Build the libbpf uProbe comparison loader" OFF)
set(LNHV1_BPF_TARGET_ARCH "x86" CACHE STRING "BPF target arch suffix, for example x86 or arm64")

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

add_executable(cross_thread_free_linux
    tests/cross_thread_free_linux.cpp
)

target_compile_options(cross_thread_free_linux
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(cross_thread_free_linux
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

add_executable(libbpf_uprobe_common_test
    tests/libbpf_uprobe_common_test.cpp
)

target_include_directories(libbpf_uprobe_common_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(libbpf_uprobe_common_test
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
add_test(NAME ablation_config_sub_ablation_stage_27 COMMAND ablation_config_test sub_ablation 27)
set_tests_properties(ablation_config_sub_ablation_stage_27 PROPERTIES
    ENVIRONMENT "LNHV1_SUBABLATION_STAGE=27")
add_test(NAME ablation_config_tracking_mode_default COMMAND ablation_config_test tracking_mode 0)
add_test(NAME ablation_config_tracking_mode_global COMMAND ablation_config_test tracking_mode 0)
set_tests_properties(ablation_config_tracking_mode_global PROPERTIES
    ENVIRONMENT "LNHV1_TRACKING_MODE=global")
add_test(NAME ablation_config_tracking_mode_sharded COMMAND ablation_config_test tracking_mode 1)
set_tests_properties(ablation_config_tracking_mode_sharded PROPERTIES
    ENVIRONMENT "LNHV1_TRACKING_MODE=sharded")
add_test(NAME ablation_config_tracking_mode_thread_local_fallback COMMAND ablation_config_test tracking_mode 2)
set_tests_properties(ablation_config_tracking_mode_thread_local_fallback PROPERTIES
    ENVIRONMENT "LNHV1_TRACKING_MODE=thread_local_fallback")
add_test(NAME ablation_config_tracking_mode_thread_local_only COMMAND ablation_config_test tracking_mode 3)
set_tests_properties(ablation_config_tracking_mode_thread_local_only PROPERTIES
    ENVIRONMENT "LNHV1_TRACKING_MODE=thread_local_only")
add_test(NAME ablation_config_tracking_mode_invalid COMMAND ablation_config_test tracking_mode 0)
set_tests_properties(ablation_config_tracking_mode_invalid PROPERTIES
    ENVIRONMENT "LNHV1_TRACKING_MODE=invalid")
add_test(NAME ablation_config_hotpath_profile_default COMMAND ablation_config_test hotpath_profile 0)
add_test(NAME ablation_config_hotpath_profile_disabled COMMAND ablation_config_test hotpath_profile 0)
set_tests_properties(ablation_config_hotpath_profile_disabled PROPERTIES
    ENVIRONMENT "LNHV1_HOTPATH_PROFILE=0")
add_test(NAME ablation_config_hotpath_profile_enabled COMMAND ablation_config_test hotpath_profile 1)
set_tests_properties(ablation_config_hotpath_profile_enabled PROPERTIES
    ENVIRONMENT "LNHV1_HOTPATH_PROFILE=1")
add_test(NAME ablation_config_hotpath_profile_invalid COMMAND ablation_config_test hotpath_profile 0)
set_tests_properties(ablation_config_hotpath_profile_invalid PROPERTIES
    ENVIRONMENT "LNHV1_HOTPATH_PROFILE=invalid")
add_test(NAME libbpf_uprobe_common COMMAND libbpf_uprobe_common_test)

add_library(hook_preload SHARED
    producer_hook/ablation_config.cpp
    producer_hook/hook_preload.cpp
    producer_hook/hook_guard.cpp
    producer_hook/hotpath_profile.cpp
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

if(LNHV1_ENABLE_LIBBPF)
    if(NOT UNIX)
        message(FATAL_ERROR "LNHV1_ENABLE_LIBBPF is only supported on Linux/Unix hosts")
    endif()

    find_program(CLANG_BIN clang REQUIRED)
    find_program(BPFTOOL_BIN bpftool REQUIRED)
    find_package(PkgConfig REQUIRED)
    find_package(ZLIB REQUIRED)
    pkg_check_modules(LIBBPF libbpf)
    pkg_check_modules(LIBELF REQUIRED libelf)

    if(NOT LIBBPF_FOUND)
        file(GLOB LNHV1_KERNEL_LIBBPF_INCLUDE_CANDIDATES
            "/usr/src/kernels/*/tools/bpf/resolve_btfids/libbpf/include"
        )
        file(GLOB LNHV1_KERNEL_LIBBPF_LIBRARY_CANDIDATES
            "/usr/src/kernels/*/tools/bpf/resolve_btfids/libbpf"
        )
        find_path(LNHV1_LIBBPF_INCLUDE_DIR
            NAMES bpf/libbpf.h
            PATHS ${LNHV1_KERNEL_LIBBPF_INCLUDE_CANDIDATES}
            NO_DEFAULT_PATH
        )
        find_library(LNHV1_LIBBPF_LIBRARY
            NAMES bpf libbpf
            PATHS ${LNHV1_KERNEL_LIBBPF_LIBRARY_CANDIDATES}
            NO_DEFAULT_PATH
        )
        if(NOT LNHV1_LIBBPF_INCLUDE_DIR OR NOT LNHV1_LIBBPF_LIBRARY)
            message(FATAL_ERROR
                "libbpf development files not found. Install libbpf-devel, "
                "or provide kernel libbpf headers/library under /usr/src/kernels.")
        endif()

        set(LIBBPF_INCLUDE_DIRS "${LNHV1_LIBBPF_INCLUDE_DIR}")
        set(LIBBPF_LIBRARIES "${LNHV1_LIBBPF_LIBRARY}")
        set(LIBBPF_LIBRARY_DIRS "")
        message(STATUS "Using fallback kernel libbpf: ${LNHV1_LIBBPF_LIBRARY}")
    endif()

    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_INCLUDES ${LIBBPF_INCLUDE_DIRS})
    check_cxx_source_compiles("
        #include <bpf/libbpf.h>
        int main() {
            struct bpf_uprobe_opts opts = {};
            opts.sz = sizeof(opts);
            opts.func_name = \"malloc\";
            return opts.func_name == nullptr;
        }
    " LNHV1_HAS_LIBBPF_UPROBE_FUNC_NAME)
    unset(CMAKE_REQUIRED_INCLUDES)

    set(EBPF_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/ebpf_probe")
    set(VMLINUX_H "${EBPF_BUILD_DIR}/vmlinux.h")
    set(BPF_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ebpf_probe/uprobe_probe.bpf.c")
    set(BPF_COMMON_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/ebpf_probe/uprobe_common.h")
    set(BPF_OBJECT "${EBPF_BUILD_DIR}/uprobe_probe.bpf.o")
    set(BPF_SKEL "${EBPF_BUILD_DIR}/uprobe_probe.skel.h")

    file(MAKE_DIRECTORY "${EBPF_BUILD_DIR}")

    add_custom_command(
        OUTPUT "${VMLINUX_H}"
        COMMAND /bin/sh -c "${BPFTOOL_BIN} btf dump file /sys/kernel/btf/vmlinux format c > '${VMLINUX_H}'"
        COMMENT "Generating vmlinux.h from /sys/kernel/btf/vmlinux"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${BPF_OBJECT}"
        COMMAND "${CLANG_BIN}"
            -g
            -O2
            -target bpf
            "-D__TARGET_ARCH_${LNHV1_BPF_TARGET_ARCH}"
            "-I${EBPF_BUILD_DIR}"
            "-I${CMAKE_CURRENT_SOURCE_DIR}"
            -c "${BPF_SOURCE}"
            -o "${BPF_OBJECT}"
        DEPENDS "${BPF_SOURCE}" "${BPF_COMMON_HEADER}" "${VMLINUX_H}"
        COMMENT "Building libbpf uProbe BPF object"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${BPF_SKEL}"
        COMMAND /bin/sh -c "${BPFTOOL_BIN} gen skeleton '${BPF_OBJECT}' > '${BPF_SKEL}'"
        DEPENDS "${BPF_OBJECT}"
        COMMENT "Generating libbpf skeleton"
        VERBATIM
    )

    add_custom_target(libbpf_uprobe_skeleton DEPENDS "${BPF_SKEL}")

    add_executable(libbpf_uprobe_loader
        ebpf_probe/uprobe_loader.cpp
    )

    add_dependencies(libbpf_uprobe_loader libbpf_uprobe_skeleton)

    target_include_directories(libbpf_uprobe_loader
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            "${EBPF_BUILD_DIR}"
            ${LIBBPF_INCLUDE_DIRS}
            ${LIBELF_INCLUDE_DIRS}
    )

    target_link_directories(libbpf_uprobe_loader
        PRIVATE
            ${LIBBPF_LIBRARY_DIRS}
            ${LIBELF_LIBRARY_DIRS}
    )

    target_link_libraries(libbpf_uprobe_loader
        PRIVATE
            ${LIBBPF_LIBRARIES}
            ${LIBELF_LIBRARIES}
            ZLIB::ZLIB
    )

    target_compile_options(libbpf_uprobe_loader
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )

    if(LNHV1_HAS_LIBBPF_UPROBE_FUNC_NAME)
        target_compile_definitions(libbpf_uprobe_loader
            PRIVATE
                LNHV1_HAS_LIBBPF_UPROBE_FUNC_NAME=1
        )
    endif()
endif()
