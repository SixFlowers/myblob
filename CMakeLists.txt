cmake_minimum_required(VERSION 3.10)
project(MyBlob)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/include)

find_package(OpenSSL REQUIRED)

set(SOURCES
    src/cloud/provider.cpp
    src/cloud/http_provider.cpp
    src/cloud/transaction.cpp
    src/network/http_client.cpp
    src/network/connection.cpp
    src/network/connection_mannager.cpp
    src/network/http_request.cpp
    src/network/http_response.cpp
    src/network/message_result.cpp
    src/network/original_message.cpp
    src/network/http_helper.cpp
)

add_executable(simple_download example/sync_example.cpp ${SOURCES})

target_link_libraries(simple_download OpenSSL::SSL OpenSSL::Crypto pthread)
