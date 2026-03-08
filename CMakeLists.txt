cmake_minimum_required(VERSION 3.16)
project(VehicleRobotBaseFramework VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_TESTS "Build unit tests" ON)
option(ENABLE_SANITIZER "Enable address/undefined sanitizers for Debug on GCC/Clang" OFF)
option(ENABLE_STRICT_WARNINGS "Enable strict warnings as errors" ON)

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

include_directories(${CMAKE_SOURCE_DIR}/src)

add_library(vr_error_code STATIC src/core/error_code.cpp)

add_library(commonsvc_log STATIC src/log/logger.cpp)
target_link_libraries(commonsvc_log PUBLIC vr_error_code)

add_library(vr_base_framework
    src/core/thread_pool.cpp
    src/core/process_guardian.cpp
    src/core/retry_policy.cpp
    src/ipc/posix_message_queue.cpp
    src/interconnect/message_codec.cpp
    src/interconnect/message_router.cpp
    src/interconnect/posix_mq_transport.cpp
    src/interconnect/interconnect_bridge.cpp
)

target_link_libraries(vr_base_framework
    PUBLIC commonsvc_log vr_error_code
)

if(UNIX AND NOT APPLE)
    target_link_libraries(vr_base_framework PUBLIC rt pthread)
elseif(UNIX)
    target_link_libraries(vr_base_framework PUBLIC pthread)
endif()

add_executable(demo_service demo/main.cpp)
target_link_libraries(demo_service PRIVATE vr_base_framework)

add_executable(interconnect_learning_demo demo/interconnect_learning_demo.cpp)
target_link_libraries(interconnect_learning_demo PRIVATE vr_base_framework)

if(ENABLE_TESTS)
    enable_testing()

    add_executable(thread_pool_test tests/thread_pool_test.cpp)
    target_link_libraries(thread_pool_test PRIVATE vr_base_framework)
    add_test(NAME thread_pool_test COMMAND thread_pool_test)
    set_tests_properties(thread_pool_test PROPERTIES LABELS "core" TIMEOUT 10)

    add_executable(retry_policy_test tests/core/retry_policy_test.cpp)
    target_link_libraries(retry_policy_test PRIVATE vr_base_framework)
    add_test(NAME retry_policy_test COMMAND retry_policy_test)
    set_tests_properties(retry_policy_test PROPERTIES LABELS "core" TIMEOUT 10)

    add_executable(posix_message_queue_test tests/ipc/posix_message_queue_test.cpp)
    target_link_libraries(posix_message_queue_test PRIVATE vr_base_framework)
    add_test(NAME posix_message_queue_test COMMAND posix_message_queue_test)
    set_tests_properties(posix_message_queue_test PROPERTIES LABELS "ipc" TIMEOUT 10)

    add_executable(logger_format_test tests/log/logger_format_test.cpp)
    target_link_libraries(logger_format_test PRIVATE vr_base_framework)
    add_test(NAME logger_format_test COMMAND logger_format_test)
    set_tests_properties(logger_format_test PROPERTIES LABELS "log" TIMEOUT 10)

    add_executable(interconnect_codec_test tests/interconnect/interconnect_codec_test.cpp)
    target_link_libraries(interconnect_codec_test PRIVATE vr_base_framework)
    add_test(NAME interconnect_codec_test COMMAND interconnect_codec_test)
    set_tests_properties(interconnect_codec_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(interconnect_ttl_test tests/interconnect/interconnect_ttl_test.cpp)
    target_link_libraries(interconnect_ttl_test PRIVATE vr_base_framework)
    add_test(NAME interconnect_ttl_test COMMAND interconnect_ttl_test)
    set_tests_properties(interconnect_ttl_test PROPERTIES LABELS "interconnect" TIMEOUT 10)

    add_executable(interconnect_router_exception_test tests/interconnect/interconnect_router_exception_test.cpp)
    target_link_libraries(interconnect_router_exception_test PRIVATE vr_base_framework)
    add_test(NAME interconnect_router_exception_test COMMAND interconnect_router_exception_test)
    set_tests_properties(interconnect_router_exception_test PROPERTIES LABELS "interconnect" TIMEOUT 10)
endif()
