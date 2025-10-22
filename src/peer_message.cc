#include "peer_message.h"

// 包含实现所需的其他标准库
#include <sstream>      // 用于 std::stringstream
#include <utility>      // 用于 std::move
#include <stdexcept>    // 用于 std::exception (在反序列化失败时)
#include <ctime>        // 用于 std::time (在 make_text 中)

Message::Message(SenderInfo sender, std::string content, std::time_t timestamp, MessageContentType type)
    : sender(std::move(sender)),       
      content(std::move(content)),     
      timestamp(timestamp),
      content_type(type) {
}

std::string Message::serialize() const {
    std::stringstream ss;
    {
        cereal::PortableBinaryOutputArchive oarchive(ss);
        oarchive(*this);
    }
    return ss.str();
}

Message Message::deserialize(const std::string& data) {
    std::stringstream ss(data);
    Message msg; // 创建一个默认的 Message 对象用于加载数据

    try {
        cereal::PortableBinaryInputArchive iarchive(ss);
        iarchive(msg);

    } catch (const std::exception& e) {
        return Message::make_empty();
    }

    return msg; 
}

/**
 * @brief 创建一个文本消息
 */
Message Message::make_text(SenderInfo sender, std::string content) {
    
    return Message(std::move(sender),          
                   std::move(content),         
                   std::time(nullptr),     
                   MessageContentType::MSG_TEXT);
}

/**
 * @brief 创建一个空消息
 */
Message Message::make_empty() {
    return Message(); 
}