cmake_minimum_required(VERSION 3.0.0)

project(YATinyWinFTP2 VERSION 0.2.0)


# Building server
add_executable(YATinyWinFTP2
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

set_property(TARGET YATinyWinFTP2 PROPERTY CXX_STANDARD 23)

target_include_directories(YATinyWinFTP2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../Tools/asio/installed/include)

target_link_libraries(YATinyWinFTP2
    pthread
)
