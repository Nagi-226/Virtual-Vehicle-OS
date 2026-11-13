cmake_minimum_required(VERSION 3.16)
project(VehicleRobotBaseFramework VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_TESTS "Build unit tests" ON)
option(ENABLE_SANITIZER "Enable address/undefined sanitizers for Debug on GCC/Clang" OFF)
option(ENABLE_STRICT_WARNINGS "Enable strict warnings as errors" ON)
option(ENABLE_STM32F429_TARGET "Build STM32F429ZGT6 minimal firmware target" OFF)

if(ENABLE_STRICT_WARNINGS)
    if(MSVC)
        add_compile_options(/W4 /WX)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

if(ENABLE_SANITIZER AND NOT MSVC)
    add_compile_options(
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
    )
    add_link_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>)
endif()

# 注意：必须用 CMAKE_CURRENT_SOURCE_DIR（本文件所在目录），不能用 CMAKE_SOURCE_DIR。
# 后者指向**当前工程顶层**：当 VVOS 被父工程 add_subdirectory() 嵌入时，它指向父工程根，
# 于是这里的 -I 变成 <父工程>/src —— VVOS 自己的头文件全部找不到（实测 13 个 TU 全部
# "fatal error: xxx.hpp: No such file or directory"）。CMAKE_CURRENT_SOURCE_DIR 在
# 两种情况下都等于 VVOS 根，语义正确。
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(vr_error_code STATIC src/core/error_code.cpp)

add_library(commonsvc_log STATIC src/log/logger.cpp)
target_link_libraries(commonsvc_log PUBLIC vr_error_code)

set(VR_BASE_SOURCES
    src/core/thread_pool.cpp
    src/core/retry_policy.cpp
    src/interconnect/message_codec.cpp
    src/interconnect/message_protocol_adapter.cpp
    src/interconnect/message_router.cpp
    src/interconnect/interconnect_bridge.cpp
)

if(NOT WIN32)
    list(APPEND VR_BASE_SOURCES
        src/core/process_guardian.cpp
        src/ipc/posix_message_queue.cpp
        src/interconnect/posix_mq_transport.cpp
    )
endif()

list(APPEND VR_BASE_SOURCES
    src/interconnect/tcp_transport.cpp
    src/interconnect/policy_manager.cpp
    src/interconnect/protocol_manager.cpp
    src/interconnect/diagnostics_manager.cpp
    src/interconnect/transport_orchestrator.cpp
)

add_library(vr_base_framework ${VR_BASE_SOURCES})

# 把 src/ 作为**导出**的头文件目录给消费者。
#
# 为什么必须单独写这一条：上面 :34 的 include_directories() 是**目录级**作用域，只影响
# 本目录及其子目录里创建的目标，**不会传播给父工程**。所以父工程即使
# `target_link_libraries(app PRIVATE vr_base_framework)`，也拿不到 -I，
# `#include "interconnect/message_codec.hpp"` 直接 fatal error（实测：
# `main.cpp:10: fatal error: interconnect/message_codec.hpp: No such file or directory`）。
#
# PUBLIC 而不是 INTERFACE：本库自身编译也要这些头（公有 include 会自动参与本 target 的编译），
# 语义就是"用这个 target 的人也需要这些头"，不是"额外多给"。
target_include_directories(vr_base_framework
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(vr_base_framework
    PUBLIC commonsvc_log vr_error_code
)

if(UNIX AND NOT APPLE)
    target_link_libraries(vr_base_framework PUBLIC rt pthread)
elseif(UNIX)
    target_link_libraries(vr_base_framework PUBLIC pthread)
elseif(WIN32)
    # tcp_transport.cpp 需要 Winsock。这里显式链接，而不是在源码里写
    # MSVC 专有的 #pragma comment(lib, ...)：后者对 GCC/Clang(MinGW) 无效且
    # 在 -Werror 下会因 -Wunknown-pragmas 直接编失败。
    target_link_libraries(vr_base_framework PUBLIC ws2_32)
endif()

if(ENABLE_STM32F429_TARGET)
    add_executable(stm32f429_min_fw
        platform/stm32f429/startup_stm32f429xx_min.c
        platform/stm32f429/system_stm32f429_min.c
        platform/stm32f429/stm32f429_hal_min.c
        platform/stm32f429/uart_log_min.c
        platform/stm32f429/main_min.cpp
    )
    target_include_directories(stm32f429_min_fw PRIVATE platform/stm32f429)
    # 链接脚本是本仓库内的文件，同样必须用 CMAKE_CURRENT_SOURCE_DIR（理由见文件头部
    # include_directories 处的说明）：用 CMAKE_SOURCE_DIR 会指向父工程根。
    target_link_options(stm32f429_min_fw PRIVATE
        -T${CMAKE_CURRENT_SOURCE_DIR}/platform/stm32f429/STM32F429ZGTX_FLASH.ld
        -nostartfiles
    )

        if(CMAKE_OBJCOPY)
            add_custom_command(TARGET stm32f429_min_fw POST_BUILD
                COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:stm32f429_min_fw> $<TARGET_FILE_DIR:stm32f429_min_fw>/stm32f429_min_fw.hex
                COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:stm32f429_min_fw> $<TARGET_FILE_DIR:stm32f429_min_fw>/stm32f429_min_fw.bin
                COMMENT "Generating STM32F429 HEX/BIN artifacts"
            )
        endif()

        # 显式下架说明（诊断 A14）：该目标当前**不可链接**，所以默认 OFF；
        # 一旦打开就在这里失败，而不是让构建产出一个链接失败的半成品。
        # 已核实的缺口（静态证据；本机无 arm-none-eabi-gcc，未做交叉链接验证）：
        #   1) 源列表缺 platform/stm32f429/f429_bridge_diag_adapter.cpp，
        #      但 main_min.cpp:29 调用了 F429BridgeDiagAdapter::EmitSramBringupEvent；
        #   2) 该适配器调用 DiagnosticsManager::RecordSnapshotWithFields，
        #      其实现 src/interconnect/diagnostics_manager.cpp 同样不在源列表里；
        #   3) main_min.cpp 使用 std::string / std::unordered_map，需要 C++ 运行库，
        #      而当前链接选项只有 -nostartfiles。
        # 恢复条件：补齐上述 TU，并在装有 arm-none-eabi-gcc 的环境完成一次真实链接。
        message(FATAL_ERROR
            "stm32f429_min_fw 目标当前不可链接（缺 f429_bridge_diag_adapter.cpp 与 "
            "diagnostics_manager.cpp，且 C++ 运行库配置未定型），已显式下架。"
            "详见 README 的 STM32F429 章节。")
    endif()

if(NOT WIN32)
    add_executable(demo_service demo/main.cpp)
    target_link_libraries(demo_service PRIVATE vr_base_framework)

    add_executable(interconnect_learning_demo demo/interconnect_learning_demo.cpp)
    target_link_libraries(interconnect_learning_demo PRIVATE vr_base_framework)

    add_executable(interconnect_benchmark_driver demo/interconnect_benchmark_driver.cpp)
    target_link_libraries(interconnect_benchmark_driver PRIVATE vr_base_framework)

    add_executable(rtos_adapter_sample demo/rtos_adapter_sample.cpp)
    target_link_libraries(rtos_adapter_sample PRIVATE vr_base_framework)

    add_executable(ros2_adapter_sample demo/ros2_adapter_sample.cpp)
    target_link_libraries(ros2_adapter_sample PRIVATE vr_base_framework)
endif()

if(ENABLE_TESTS)
    enable_testing()

    # 注册测试的唯一入口。它做两件事：
    #   1) 声明 ctest 测试（add_test）；
    #   2) 把名字累加到 VVOS_DECLARED_TESTS，最终落到
    #      ${CMAKE_CURRENT_BINARY_DIR}/vvos_declared_tests.txt。
    #      （用 CURRENT_BINARY_DIR 而不是 BINARY_DIR：后者是**整个工程**的构建根，
    #        被父工程 add_subdirectory() 嵌入时会写到父工程构建根去，属目录泄漏。
    #        独立构建时两者相同，行为不变。）
    # 质量门禁会用这份清单与 `ctest -N` 的注册结果做双向比对，
    # 阻断"声明了却没注册 / 注册了却没声明 / 一个测试都没跑"的静默漂移。
    # 新增测试请一律走这个宏，不要再直接写 add_test。
    set(VVOS_DECLARED_TESTS "")
    macro(vvos_register_test test_name test_command)
        add_test(NAME ${test_name} COMMAND ${test_command})
        set(VVOS_DECLARED_TESTS ${VVOS_DECLARED_TESTS} ${test_name})
    endmacro()

    add_executable(thread_pool_test tests/thread_pool_test.cpp)
    target_link_libraries(thread_pool_test PRIVATE vr_base_framework)
    vvos_register_test(thread_pool_test thread_pool_test)
    set_tests_properties(thread_pool_test PROPERTIES LABELS "core" TIMEOUT 10)

    add_executable(retry_policy_test tests/core/retry_policy_test.cpp)
    target_link_libraries(retry_policy_test PRIVATE vr_base_framework)
    vvos_register_test(retry_policy_test retry_policy_test)
    set_tests_properties(retry_policy_test PROPERTIES LABELS "core" TIMEOUT 10)

    add_executable(logger_format_test tests/log/logger_format_test.cpp)
    target_link_libraries(logger_format_test PRIVATE vr_base_framework)
    vvos_register_test(logger_format_test logger_format_test)
    set_tests_properties(logger_format_test PROPERTIES LABELS "log" TIMEOUT 10)

    add_executable(interconnect_codec_test tests/interconnect/interconnect_codec_test.cpp)
    target_link_libraries(interconnect_codec_test PRIVATE vr_base_framework)
    vvos_register_test(interconnect_codec_test interconnect_codec_test)
    set_tests_properties(interconnect_codec_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(interconnect_ttl_test tests/interconnect/interconnect_ttl_test.cpp)
    target_link_libraries(interconnect_ttl_test PRIVATE vr_base_framework)
    vvos_register_test(interconnect_ttl_test interconnect_ttl_test)
    set_tests_properties(interconnect_ttl_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(interconnect_router_exception_test tests/interconnect/interconnect_router_exception_test.cpp)
    target_link_libraries(interconnect_router_exception_test PRIVATE vr_base_framework)
    vvos_register_test(interconnect_router_exception_test interconnect_router_exception_test)
    set_tests_properties(interconnect_router_exception_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(interconnect_backpressure_test tests/interconnect/interconnect_backpressure_test.cpp)
    target_link_libraries(interconnect_backpressure_test PRIVATE vr_base_framework)
    vvos_register_test(interconnect_backpressure_test interconnect_backpressure_test)
    set_tests_properties(interconnect_backpressure_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(interconnect_metrics_delta_test tests/interconnect/interconnect_metrics_delta_test.cpp)
    target_link_libraries(interconnect_metrics_delta_test PRIVATE vr_base_framework)
    vvos_register_test(interconnect_metrics_delta_test interconnect_metrics_delta_test)
    set_tests_properties(interconnect_metrics_delta_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(interconnect_config_provider_test tests/interconnect/interconnect_config_provider_test.cpp)
    target_link_libraries(interconnect_config_provider_test PRIVATE vr_base_framework)
    vvos_register_test(interconnect_config_provider_test interconnect_config_provider_test)
    set_tests_properties(interconnect_config_provider_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(interconnect_config_reload_test tests/interconnect/interconnect_config_reload_test.cpp)
    target_link_libraries(interconnect_config_reload_test PRIVATE vr_base_framework)
    vvos_register_test(interconnect_config_reload_test interconnect_config_reload_test)
    set_tests_properties(interconnect_config_reload_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(interconnect_policy_matrix_test tests/interconnect/interconnect_policy_matrix_test.cpp)
    target_link_libraries(interconnect_policy_matrix_test PRIVATE vr_base_framework)
    vvos_register_test(interconnect_policy_matrix_test interconnect_policy_matrix_test)
    set_tests_properties(interconnect_policy_matrix_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(interconnect_transport_consistency_test tests/interconnect/interconnect_transport_consistency_test.cpp)
    target_link_libraries(interconnect_transport_consistency_test PRIVATE vr_base_framework)
    vvos_register_test(interconnect_transport_consistency_test interconnect_transport_consistency_test)
    set_tests_properties(interconnect_transport_consistency_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(transport_orchestrator_test tests/interconnect/transport_orchestrator_test.cpp)
    target_link_libraries(transport_orchestrator_test PRIVATE vr_base_framework)
    vvos_register_test(transport_orchestrator_test transport_orchestrator_test)
    set_tests_properties(transport_orchestrator_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(interconnect_fault_injection_test tests/interconnect/interconnect_fault_injection_test.cpp)
    target_link_libraries(interconnect_fault_injection_test PRIVATE vr_base_framework)
    vvos_register_test(interconnect_fault_injection_test interconnect_fault_injection_test)
    set_tests_properties(interconnect_fault_injection_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(interconnect_tcp_loopback_test tests/interconnect/interconnect_tcp_loopback_test.cpp)
    target_link_libraries(interconnect_tcp_loopback_test PRIVATE vr_base_framework)
    vvos_register_test(interconnect_tcp_loopback_test interconnect_tcp_loopback_test)
    set_tests_properties(interconnect_tcp_loopback_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(interconnect_protocol_adapter_test tests/interconnect/interconnect_protocol_adapter_test.cpp)
    target_link_libraries(interconnect_protocol_adapter_test PRIVATE vr_base_framework)
    vvos_register_test(interconnect_protocol_adapter_test interconnect_protocol_adapter_test)
    set_tests_properties(interconnect_protocol_adapter_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(rtos_adapter_stub_test tests/platform/rtos_adapter_stub_test.cpp)
    target_link_libraries(rtos_adapter_stub_test PRIVATE vr_base_framework)
    vvos_register_test(rtos_adapter_stub_test rtos_adapter_stub_test)
    set_tests_properties(rtos_adapter_stub_test PROPERTIES LABELS "platform" TIMEOUT 10)

    add_executable(ros2_adapter_stub_test tests/platform/ros2_adapter_stub_test.cpp)
    target_link_libraries(ros2_adapter_stub_test PRIVATE vr_base_framework)
    vvos_register_test(ros2_adapter_stub_test ros2_adapter_stub_test)
    set_tests_properties(ros2_adapter_stub_test PROPERTIES LABELS "platform" TIMEOUT 10)

    # f429_bridge_diag_adapter.cpp 本身是平台无关的纯 C++（std::string/unordered_map），
    # 但从未被任何目标编译过：这里补进本测试目标，否则 EmitSramBringupEvent 会
    # 在链接期未定义。include 路径用仓库根，因为源码头用的是
    # "platform/stm32f429/..." 这种根相对写法（与 CMakeLists 只加 src/ 的约定不同）。
    add_executable(f429_bridge_diag_adapter_test
        tests/platform/f429_bridge_diag_adapter_test.cpp
        platform/stm32f429/f429_bridge_diag_adapter.cpp
    )
    # 这里要加的是**本仓库根**（下面源码头用的是 "platform/stm32f429/..." 根相对写法），
    # 所以用 CMAKE_CURRENT_SOURCE_DIR；用 CMAKE_SOURCE_DIR 在被父工程嵌入时会指向父工程根。
    target_include_directories(f429_bridge_diag_adapter_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(f429_bridge_diag_adapter_test PRIVATE vr_base_framework)
    vvos_register_test(f429_bridge_diag_adapter_test f429_bridge_diag_adapter_test)
    set_tests_properties(f429_bridge_diag_adapter_test PROPERTIES LABELS "platform" TIMEOUT 10)

    if(NOT WIN32)
        vvos_register_test(rtos_adapter_sample_run rtos_adapter_sample)
        set_tests_properties(rtos_adapter_sample_run PROPERTIES LABELS "platform" TIMEOUT 10)

        vvos_register_test(ros2_adapter_sample_run ros2_adapter_sample)
        set_tests_properties(ros2_adapter_sample_run PROPERTIES LABELS "platform" TIMEOUT 10)
        add_executable(posix_message_queue_test tests/ipc/posix_message_queue_test.cpp)
        target_link_libraries(posix_message_queue_test PRIVATE vr_base_framework)
        vvos_register_test(posix_message_queue_test posix_message_queue_test)
        set_tests_properties(posix_message_queue_test PROPERTIES LABELS "ipc" TIMEOUT 10)
    endif()

    # 落盘声明清单：门禁据此校验"声明数 == 注册数"，且清单已按本平台的实际
    # 条件分支求值（例如 NOT WIN32 里的 3 条在 Windows 上不会出现）。
    # 用 CMAKE_CURRENT_BINARY_DIR：被父工程嵌入时清单落在 VVOS 自己的构建子目录，
    # 不污染父工程构建根；独立构建时与 CMAKE_BINARY_DIR 相同。
    string(REPLACE ";" "\n" VVOS_DECLARED_TESTS_TEXT "${VVOS_DECLARED_TESTS}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/vvos_declared_tests.txt" "${VVOS_DECLARED_TESTS_TEXT}\n")
endif()
