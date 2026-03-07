# CMake generated Testfile for 
# Source directory: /mnt/e/Virtual-Vehicle-OS
# Build directory: /mnt/e/Virtual-Vehicle-OS/build-linux
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(thread_pool_test "/mnt/e/Virtual-Vehicle-OS/build-linux/thread_pool_test")
set_tests_properties(thread_pool_test PROPERTIES  LABELS "core" TIMEOUT "10" _BACKTRACE_TRIPLES "/mnt/e/Virtual-Vehicle-OS/CMakeLists.txt;60;add_test;/mnt/e/Virtual-Vehicle-OS/CMakeLists.txt;0;")
add_test(retry_policy_test "/mnt/e/Virtual-Vehicle-OS/build-linux/retry_policy_test")
set_tests_properties(retry_policy_test PROPERTIES  LABELS "core" TIMEOUT "10" _BACKTRACE_TRIPLES "/mnt/e/Virtual-Vehicle-OS/CMakeLists.txt;65;add_test;/mnt/e/Virtual-Vehicle-OS/CMakeLists.txt;0;")
add_test(posix_message_queue_test "/mnt/e/Virtual-Vehicle-OS/build-linux/posix_message_queue_test")
set_tests_properties(posix_message_queue_test PROPERTIES  LABELS "ipc" TIMEOUT "10" _BACKTRACE_TRIPLES "/mnt/e/Virtual-Vehicle-OS/CMakeLists.txt;70;add_test;/mnt/e/Virtual-Vehicle-OS/CMakeLists.txt;0;")
add_test(logger_format_test "/mnt/e/Virtual-Vehicle-OS/build-linux/logger_format_test")
set_tests_properties(logger_format_test PROPERTIES  LABELS "log" TIMEOUT "10" _BACKTRACE_TRIPLES "/mnt/e/Virtual-Vehicle-OS/CMakeLists.txt;75;add_test;/mnt/e/Virtual-Vehicle-OS/CMakeLists.txt;0;")
