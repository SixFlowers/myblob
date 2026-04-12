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
    src/network/poll_socket.cpp
    src/network/tasked_send_receiver.cpp
    # 暂时注释掉有问题的 TLS 文件
    # src/network/tls_connection.cpp
    # src/network/tls_context.cpp
)

add_executable(simple_download example/sync_example.cpp ${SOURCES})

add_executable(batch_example example/batch_example.cpp ${SOURCES})

add_executable(httpbin_example example/httpbin_example.cpp ${SOURCES})

add_executable(tasked_example example/tasked_example.cpp ${SOURCES})

# httpbin_https_example 需要 TLS 支持，暂时不编译
# add_executable(httpbin_https_example example/httpbin_https_example.cpp ${SOURCES})

target_link_libraries(simple_download OpenSSL::SSL OpenSSL::Crypto pthread)
target_link_libraries(batch_example OpenSSL::SSL OpenSSL::Crypto pthread)
target_link_libraries(httpbin_example OpenSSL::SSL OpenSSL::Crypto pthread)
target_link_libraries(tasked_example OpenSSL::SSL OpenSSL::Crypto pthread)
# target_link_libraries(httpbin_https_example OpenSSL::SSL OpenSSL::Crypto pthread)
