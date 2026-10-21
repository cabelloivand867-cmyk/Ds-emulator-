cmake_minimum_required(VERSION 3.16)
project(nds_emu CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

add_library(ndscore STATIC
  src/cpu.cpp
  src/system.cpp
)
target_include_directories(ndscore PUBLIC src)

add_executable(nds src/main.cpp)
target_link_libraries(nds PRIVATE ndscore)

enable_testing()
add_executable(cpu_test tests/cpu_test.cpp)
target_link_libraries(cpu_test PRIVATE ndscore)
add_test(NAME cpu_test COMMAND cpu_test)
