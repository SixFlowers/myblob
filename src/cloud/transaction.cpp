#include "cloud/transaction.hpp"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

namespace myblob::cloud
{

    Transaction::Transaction()
        : provider_(nullptr)
    {
    }

    Transaction::Transaction(Provider *provider)
        : provider_(provider)
    {
    }

    void Transaction::setProvider(Provider *provider)
    {
        provider_ = provider;
    }

    void Transaction::execute()
    {
        network::TaskedSendReceiverGroup group;
        auto handle = group.getHandle();
        if (provider_)
        {
            provider_->initCache(handle); // 初始化缓存(预留接口)
            verifyKeyRequest(handle, [this, &handle]()
                             {
            processSync(handle);
            return true; });
            return;
        }
        processSync(handle);
    }

    bool Transaction::getObjectRequest(
        const std::string &remotePath, // 文件路径
        std::pair<uint64_t, uint64_t> range,
        uint8_t *result,
        uint64_t capacity,
        uint64_t /*traceId*/
    )
    {
        if (!provider_)
        {
            return false;
        }
        auto request = provider_->getRequest(remotePath, range);
        if (!request)
        {
            return false;
        }

        auto msg = std::make_unique<network::OriginalMessage>(
            std::move(request),
            *provider_,
            result,
            capacity);

        messages_.push_back(std::move(msg));
        return true;
    }

    bool Transaction::putObjectRequest(
        const std::string &remotePath,
        const char *data,
        uint64_t size,
        uint8_t *result,
        uint64_t capacity,
        uint64_t /*traceId*/
    )
    {
        if (!provider_)
        {
            return false;
        }

        // 检查是否需要多部分上传
        if (provider_->multipartUploadSize() > 0 && size > provider_->multipartUploadSize())
        {
            // 多部分上传需要回调版本，这里简单返回false
            // 实际使用应调用带回调的模板版本
            return false;
        }

        auto request = provider_->putRequest(remotePath, std::string_view(data, size));
        if (!request)
        {
            return false;
        }

        auto msg = std::make_unique<network::OriginalMessage>(
            std::move(request),
            *provider_,
            result,
            capacity);
        msg->setPutRequestData(reinterpret_cast<const uint8_t *>(data), size);

        messages_.push_back(std::move(msg));
        return true;
    }

    bool Transaction::deleteObjectRequest(
        const std::string &remotePath,
        uint8_t *result,
        uint64_t capacity,
        uint64_t /*traceId*/
    )
    {
        if (!provider_)
        {
            return false;
        }
        auto request = provider_->deleteRequest(remotePath);
        if (!request)
        {
            return false;
        }

        auto msg = std::make_unique<network::OriginalMessage>(
            std::move(request),
            *provider_,
            result,
            capacity);

        messages_.push_back(std::move(msg));
        return true;
    }
    // handle:专属调度器
    void Transaction::processSync(network::TaskedSendReceiverHandle &sendReceiverHandle)
    {
        do
        {
            // messageCounter_是一个原子计数器，记录已经发送的消息数量
            for (; messageCounter_ < messages_.size(); messageCounter_++)
            {
                sendReceiverHandle.sendSync(messages_[messageCounter_].get());
            }

            // multipartUploads_是一个存储多部分上传信息的容器,multipart是一个多部分上传的实例(一个完整文件的分片上传任务)
            for (auto &multipart : multipartUploads_)
            {
                if (multipart.state == MultipartUpload::State::Sending)
                {
                    for (auto i = 0ull; i < multipart.eTags.size(); i++)
                    {
                        sendReceiverHandle.sendSync(multipart.messages[i].get());
                    }
                    multipart.state = MultipartUpload::State::Processing;
                    // MultipartUpload::State::Validating 分片全传完，Complete消息已就绪
                }
                else if (multipart.state == MultipartUpload::State::Validating)
                {
                    sendReceiverHandle.sendSync(multipart.messages[multipart.eTags.size()].get()); // 发送"完全上传"请求,complete请求
                    multipart.state = MultipartUpload::State::Default;                             //// 重置，防止下一轮重复
                }
            }

            sendReceiverHandle.processSync(); // ③ 驱动事件循环，等所有 IO 完成
        } while (multipartUploads_.size() != completedMultiparts_);
        // multipartUploads_.size()是客户端自己计算的
    }
    // group共享调度器
    bool Transaction::processAsync(network::TaskedSendReceiverGroup &group)
    {
        std::vector<network::OriginalMessage *> submissions;
        auto multiPartSize = 0ull;
        for (auto &multipart : multipartUploads_)
        {
            multiPartSize += multipart.messages.size();
        }
        submissions.reserve(messages_.size() + multiPartSize);

        auto previousCounter = messageCounter_.load();
        for (; messageCounter_ < messages_.size(); messageCounter_++)
        {
            submissions.emplace_back(messages_[messageCounter_].get());
        }

        for (auto &multipart : multipartUploads_)
        {
            if (multipart.state == MultipartUpload::State::Sending)
            {
                for (auto i = 0ull; i < multipart.eTags.size(); i++)
                {
                    submissions.emplace_back(multipart.messages[i].get());
                }
                multipart.state = MultipartUpload::State::Processing;
            }
            else if (multipart.state == MultipartUpload::State::Validating)
            {
                submissions.emplace_back(multipart.messages[multipart.eTags.size()].get());
                multipart.state = MultipartUpload::State::Default;
            }
        }

        if (submissions.empty())
        {
            return true;
        }

        auto success = group.send(submissions);
        if (!success)
        {
            messageCounter_ = previousCounter;
            for (auto &multipart : multipartUploads_)
            {
                if (multipart.state == MultipartUpload::State::Processing)
                {
                    multipart.state = MultipartUpload::State::Sending;
                }
                else if (multipart.state == MultipartUpload::State::Default)
                {
                    multipart.state = MultipartUpload::State::Validating;
                }
            }
        }
        return success;
    }

    // 迭代器实现
    Transaction::Iterator Transaction::begin()
    {
        return Iterator(messages_.begin());
    }

    Transaction::Iterator Transaction::end()
    {
        return Iterator(messages_.end());
    }

    Transaction::ConstIterator Transaction::cbegin() const
    {
        return ConstIterator(messages_.cbegin());
    }

    Transaction::ConstIterator Transaction::cend() const
    {
        return ConstIterator(messages_.cend());
    }

} // namespace myblob::cloud
