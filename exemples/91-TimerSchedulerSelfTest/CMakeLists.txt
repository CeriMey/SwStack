cmake_minimum_required(VERSION 3.10)
project(TimerSchedulerSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(TimerSchedulerSelfTest TimerSchedulerSelfTest.cpp)
target_include_directories(TimerSchedulerSelfTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(WIN32)
    target_link_libraries(TimerSchedulerSelfTest PRIVATE ws2_32)
endif()

enable_testing()
add_test(NAME TimerSchedulerSelfTest COMMAND TimerSchedulerSelfTest)
