cmake_minimum_required(VERSION 3.11)

set(PROJECT_NAME rc)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")

project(${PROJECT_NAME} CXX)

file(GLOB_RECURSE SOURCES "src/*.cpp")

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(${PROJECT_NAME} ${SOURCES})
