cmake_minimum_required(VERSION 3.10)
project(SwOrderedTerminationSelfTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(SwOrderedTerminationSelfTest
  SwOrderedTerminationSelfTest.cpp)
target_include_directories(SwOrderedTerminationSelfTest PRIVATE
  ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(SwOrderedTerminationSelfTest PRIVATE Threads::Threads)

if(WIN32)
  target_link_libraries(SwOrderedTerminationSelfTest PRIVATE ws2_32)
endif()

enable_testing()
add_test(NAME SwOrderedTerminationSelfTest
  COMMAND SwOrderedTerminationSelfTest)
set_tests_properties(SwOrderedTerminationSelfTest PROPERTIES TIMEOUT 10)
