cmake_minimum_required(VERSION 3.28)
project(research_video_capture)

set(CMAKE_CXX_STANDARD 17)

find_package(SDL2 REQUIRED)
find_package(JPEG REQUIRED)

include_directories(${SDL2_INCLUDE_DIRS} ${JPEG_INCLUDE_DIRS})

add_executable(VideoCapture main.cpp)

target_link_libraries(VideoCapture ${SDL2_LIBRARIES} ${JPEG_LIBRARIES})