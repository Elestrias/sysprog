cmake_minimum_required(VERSION 3.20)
project(Lab5)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra -Wpedantic -Werror -Wall -Wno-gnu-folding-constant -pthread")

include_directories(../utils)


add_library(chat_client chat_client.c)
add_library(chat_server chat_server.c)
add_library(chat chat.c)

add_executable(exe_server chat_server_exe.c)
target_link_libraries(exe_server chat_server chat)

add_executable(exe_client chat_client_exe.c)
target_link_libraries(exe_client chat_client chat)

add_executable(tests test.c)
target_link_libraries(tests chat_server chat chat_client)