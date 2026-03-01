cmake_minimum_required(VERSION 3.16)
project(VehicleRobotBaseFramework VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -pthread
)

include_directories(${CMAKE_SOURCE_DIR}/src)

add_library(vr_base_framework
    src/core/error_code.cpp
    src/core/thread_pool.cpp
    src/core/process_guardian.cpp
    src/ipc/posix_message_queue.cpp
    src/log/logger.cpp
)

target_link_libraries(vr_base_framework
    rt
    pthread
)

add_executable(demo_service
    demo/main.cpp
)

target_link_libraries(demo_service
    vr_base_framework
)
