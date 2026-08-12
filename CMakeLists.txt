cmake_minimum_required(VERSION 3.10)
project(MyBlob)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/include)

find_package(OpenSSL REQUIRED)

# jemalloc: 高性能内存分配器（可选，未安装则使用系统默认 malloc）
find_package(jemalloc QUIET)
if(jemalloc_FOUND)
    message(STATUS "jemalloc found — using for improved allocation performance")
    set(JEMALLOC_LIB jemalloc)
else()
    message(STATUS "jemalloc not found — using system default malloc")
    set(JEMALLOC_LIB "")
endif()

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
    src/cloud/aws_signer.cpp          # 第九版：AWS 签名
    src/utils/utils.cpp               # 第九版：工具函数
    # 第八版新增文件
    src/network/cache.cpp
    src/network/throughput_cache.cpp
    src/network/io_uring_socket.cpp  # io_uring 支持
    # 第十一版：消息状态机
    src/network/message_task.cpp
    src/network/http_message.cpp
    src/network/https_message.cpp
    src/network/tls_connection.cpp
    src/network/tls_context.cpp
)

add_executable(simple_download example/sync_example.cpp ${SOURCES})

add_executable(batch_example example/batch_example.cpp ${SOURCES})

add_executable(httpbin_example example/httpbin_example.cpp ${SOURCES})

add_executable(tasked_example example/tasked_example.cpp ${SOURCES})

add_executable(httpbin_https_example example/httpbin_https_example.cpp ${SOURCES})

# 第九版：AWS 测试程序
add_executable(aws1_example example/aws1_example.cpp ${SOURCES})
add_executable(aws2_example example/aws2_example.cpp ${SOURCES})
add_executable(aws3_example example/aws3.example.cpp ${SOURCES})

# 第十版：新增示例
add_executable(multipartUpload_example example/multipartUpload_example.cpp ${SOURCES})
add_executable(cloud_example example/cloud_example.cpp ${SOURCES})
add_executable(providers_example example/providers_example.cpp ${SOURCES})
add_executable(minio_example example/minio_example.cpp ${SOURCES})
add_executable(minio_upload_example example/minio_upload_example.cpp ${SOURCES})
add_executable(minio_delete_example example/minio_delete_example.cpp ${SOURCES})
add_executable(minio_multipart_example example/minio_multipart_example.cpp ${SOURCES})

target_link_libraries(simple_download OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
target_link_libraries(batch_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
target_link_libraries(httpbin_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
target_link_libraries(tasked_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
target_link_libraries(httpbin_https_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
target_link_libraries(aws1_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
target_link_libraries(aws2_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
target_link_libraries(aws3_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
target_link_libraries(multipartUpload_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
target_link_libraries(cloud_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
target_link_libraries(providers_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
target_link_libraries(minio_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
target_link_libraries(minio_upload_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
target_link_libraries(minio_delete_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
target_link_libraries(minio_multipart_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})

add_executable(iouring_example example/iouring_example.cpp ${SOURCES})
target_link_libraries(iouring_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})

add_executable(async_example example/async_example.cpp ${SOURCES})
target_link_libraries(async_example OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})

# ========== 测试 ==========
add_executable(quick_test test/quick_test.cpp ${SOURCES})
target_link_libraries(quick_test OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})

add_executable(local_http_test test/local_http_test.cpp ${SOURCES})
target_link_libraries(local_http_test OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})

add_executable(cloud_test test/cloud_test.cpp ${SOURCES})
target_link_libraries(cloud_test OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})

add_executable(minio_test test/minio_test.cpp ${SOURCES})
target_link_libraries(minio_test OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})

add_executable(multipart_test test/multipart_test.cpp ${SOURCES})
target_link_libraries(multipart_test OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})

add_executable(stress_test test/stress_test.cpp ${SOURCES})
target_link_libraries(stress_test OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})

add_executable(myblob_test test/test_all.cpp ${SOURCES})
target_link_libraries(myblob_test OpenSSL::SSL OpenSSL::Crypto pthread uring ${JEMALLOC_LIB})
add_test(NAME myblob_unit_test COMMAND myblob_test)
