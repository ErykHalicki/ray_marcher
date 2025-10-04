cmake_minimum_required(VERSION 3.10)
project(SDL3Project)

set(CMAKE_CXX_STANDARD 11)
find_package(SDL3 REQUIRED)

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} SDL3::SDL3)