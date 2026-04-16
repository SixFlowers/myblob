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
    src/cloud/aws.cpp                 # 第九版：AWS Provider
    src/cloud/azure.cpp               # 第十版：Azure Provider
    src/cloud/azure_signer.cpp        # 第十版：Azure 签名
    src/cloud/gcp.cpp                 # 第十版：GCP Provider
    src/cloud/gcp_signer.cpp          # 第十版：GCP 签名
    src/cloud/minio.cpp               # 第十版：MinIO Provider
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
    src/network/aws_signer.cpp        # 第九版：AWS 签名
    src/utils/utils.cpp               # 第九版：工具函数
    # 第八版新增文件
    src/network/cache.cpp
    src/network/throughput_cache.cpp
    src/network/io_uring_socket.cpp  # io_uring 支持
    # 第十一版：消息状态机
    src/network/message_task.cpp
    src/network/http_message.cpp
    src/network/https_message.cpp
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

# 第九版：AWS 测试程序
add_executable(aws1_example example/aws1_example.cpp ${SOURCES})
add_executable(aws2_example example/aws2_example.cpp ${SOURCES})
add_executable(aws3_example example/aws3.example.cpp ${SOURCES})

# 第十版：新增示例
add_executable(multipartUpload_example example/multipartUpload_example.cpp ${SOURCES})
add_executable(cloud_example example/cloud_example.cpp ${SOURCES})
add_executable(providers_example example/providers_example.cpp ${SOURCES})

target_link_libraries(simple_download OpenSSL::SSL OpenSSL::Crypto pthread uring)
target_link_libraries(batch_example OpenSSL::SSL OpenSSL::Crypto pthread uring)
target_link_libraries(httpbin_example OpenSSL::SSL OpenSSL::Crypto pthread uring)
target_link_libraries(tasked_example OpenSSL::SSL OpenSSL::Crypto pthread uring)
target_link_libraries(aws1_example OpenSSL::SSL OpenSSL::Crypto pthread uring)
target_link_libraries(aws2_example OpenSSL::SSL OpenSSL::Crypto pthread uring)
target_link_libraries(aws3_example OpenSSL::SSL OpenSSL::Crypto pthread uring)
target_link_libraries(multipartUpload_example OpenSSL::SSL OpenSSL::Crypto pthread uring)
target_link_libraries(cloud_example OpenSSL::SSL OpenSSL::Crypto pthread uring)
target_link_libraries(providers_example OpenSSL::SSL OpenSSL::Crypto pthread uring)
# target_link_libraries(httpbin_https_example OpenSSL::SSL OpenSSL::Crypto pthread)
