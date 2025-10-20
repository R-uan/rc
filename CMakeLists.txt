cmake_minimum_required(VERSION 3.11)

set(PROJECT_NAME rc)

project(${PROJECT_NAME} CXX)

file(GLOB_RECURSE SOURCES "src/*.cpp")

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(${PROJECT_NAME} ${SOURCES})