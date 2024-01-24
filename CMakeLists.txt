cmake_minimum_required(VERSION 3.25.1)
project(video-defects-generator)

set(CMAKE_CXX_STANDARD 17)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    include_directories(/opt/homebrew/include)
    link_directories(/opt/homebrew/lib)
endif()

include_directories(src/header)
include_directories(/usr/local/include)
include_directories(/usr/local/opt/openssl/include)
link_directories(/usr/local/lib)
link_directories(/usr/local/opt/openssl/lib)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} pthread avutil avcodec avformat avfilter swscale)
install()
