cmake_minimum_required(VERSION 3.20)
project(BatteryGuardian LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "BatteryGuardian requires Windows")
endif()

enable_language(RC)
add_executable(BatteryGuardian WIN32 main.cpp affinity_policy.cpp log_window.cpp BatteryGuardian.rc)
target_include_directories(BatteryGuardian PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(BatteryGuardian PRIVATE cxx_std_17)
target_link_libraries(BatteryGuardian PRIVATE user32 gdi32 shell32 ole32 oleaut32 wbemuuid)
if(MSVC)
    target_compile_options(BatteryGuardian PRIVATE /W4 /utf-8)
    # The resource file embeds our administrator manifest instead of a generated one.
    target_link_options(BatteryGuardian PRIVATE /MANIFEST:NO)
elseif(MINGW)
    target_link_options(BatteryGuardian PRIVATE -municode)
endif()

add_custom_command(TARGET BatteryGuardian POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/blacklist.txt"
        "-DDESTINATION=$<TARGET_FILE_DIR:BatteryGuardian>/blacklist.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/copy_blacklist.cmake"
    VERBATIM)

option(BATTERY_GUARDIAN_BUILD_TESTS "Build affinity policy checks" OFF)
if(BATTERY_GUARDIAN_BUILD_TESTS)
    enable_testing()
    add_executable(affinity_policy_tests tests/affinity_policy_tests.cpp affinity_policy.cpp)
    target_compile_features(affinity_policy_tests PRIVATE cxx_std_17)
    target_include_directories(affinity_policy_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(affinity_policy_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    if(MSVC)
        target_compile_options(affinity_policy_tests PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME affinity_policy COMMAND affinity_policy_tests)
    add_executable(log_window_tests tests/log_window_tests.cpp log_window.cpp)
    target_compile_features(log_window_tests PRIVATE cxx_std_17)
    target_include_directories(log_window_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(log_window_tests PRIVATE user32 gdi32)
    if(MSVC)
        target_compile_options(log_window_tests PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME log_window COMMAND log_window_tests)
    add_executable(process_log_tests tests/process_log_tests.cpp)
    target_compile_features(process_log_tests PRIVATE cxx_std_17)
    target_include_directories(process_log_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(process_log_tests PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME process_log COMMAND process_log_tests)
endif()

