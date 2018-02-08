cmake_minimum_required(VERSION 3.9)
project(tiny_httpd C)

set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS "-W -Wall")
#set(CMAKE_EXE_LINKER_FLAGS "-lpthread")

set(CMAKE_BUILD_TYPE release)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

add_executable(tiny_httpd httpd.h httpd.c)