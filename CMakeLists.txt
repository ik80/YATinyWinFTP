cmake_minimum_required(VERSION 3.14)

project(YATinyWinFTP VERSION 0.0.1)

# GoogleTest requires at least C++20
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

# ASIO
FetchContent_Declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  FETCHCONTENT_TRY_FIND_PACKAGE_MODE always
)
FetchContent_MakeAvailable(asio)

# Building receiver
add_executable(YATinyWinFTP
    ${CMAKE_SOURCE_DIR}/TinyFTPReply.cpp
    ${CMAKE_SOURCE_DIR}/TinyFTPRequestHandler.cpp
    ${CMAKE_SOURCE_DIR}/TinyFTPRequestParser.cpp
    ${CMAKE_SOURCE_DIR}/TinyFTPServer.cpp
    ${CMAKE_SOURCE_DIR}/TinyFTPServer.cpp
    ${CMAKE_SOURCE_DIR}/TinyFTPSession.cpp
    ${CMAKE_SOURCE_DIR}/TinyWinFTP.cpp
)
target_include_directories(YATinyWinFTP PUBLIC
    PRIVATE ${asio_SOURCE_DIR}/asio/include
)
target_compile_definitions(YATinyWinFTP PRIVATE ASIO_STANDALONE)
