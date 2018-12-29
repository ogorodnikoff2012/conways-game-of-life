cmake_minimum_required(VERSION 2.6)
project(game_of_life)

include_directories(include)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -O0 -ggdb3")

find_package(Threads)

add_executable(game_pthread src/main.c src/common.c src/back_end/pthread.c)
target_link_libraries(game_pthread ${CMAKE_THREAD_LIBS_INIT})
