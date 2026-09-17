cmake_minimum_required(VERSION 3.16)
project(bleed_detection_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio)

add_executable(bleed_detection src/main.cpp)
target_link_libraries(bleed_detection PRIVATE ${OpenCV_LIBS})
target_include_directories(bleed_detection PRIVATE ${OpenCV_INCLUDE_DIRS})

if (MSVC)
    target_compile_options(bleed_detection PRIVATE /W4 /permissive-)
else()
    target_compile_options(bleed_detection PRIVATE -Wall -Wextra -Wpedantic)
endif()
