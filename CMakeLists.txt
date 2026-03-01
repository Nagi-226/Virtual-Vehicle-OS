cmake_minimum_required(VERSION 3.16)
project(VehicleRobotBaseFramework VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

include_directories(${CMAKE_SOURCE_DIR}/src)

add_library(vr_base_framework
    src/core/error_code.cpp
    src/core/thread_pool.cpp
    src/core/process_guardian.cpp
    src/ipc/posix_message_queue.cpp
    src/log/logger.cpp
)

if(UNIX AND NOT APPLE)
    target_link_libraries(vr_base_framework rt pthread)
elseif(UNIX)
    target_link_libraries(vr_base_framework pthread)
endif()

add_executable(demo_service demo/main.cpp)
target_link_libraries(demo_service vr_base_framework)
