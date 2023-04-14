cmake_minimum_required(VERSION 3.19)

project(Lab4 C)

set(CMAKE_C_STANDARD 11)


SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
find_package (Threads REQUIRED)

add_compile_options(-fsanitize=address)
add_link_options(-fsanitize=address)


add_library(thread_pool thread_pool.c)
target_link_libraries(thread_pool   ${CMAKE_THREAD_LIBS_INIT})
include_directories(../utils)

add_executable(testing test.c)
target_link_libraries(testing
        thread_pool)