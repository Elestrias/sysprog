cmake_minimum_required(VERSION 3.24)
project(SysProga2 C)

set(CMAKE_C_STANDARD 17)

#set(HHREPORT 1)
#add_link_options(-ldl -lutil)
add_compile_options(-fsanitize=address)
add_link_options(-fsanitize=address)


#add_library(heap_help ../utils/heap_help/heap_help.c ../utils/heap_help/heap_help.h)
#include_directories(../utils/)



add_executable(SysProga2 main.c parser.c)
#target_link_libraries(SysProga2 heap_help)