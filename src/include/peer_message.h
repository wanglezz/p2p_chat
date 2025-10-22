#pragma once

#include <string>
#include <ctime>
#include <sstream>


#include "cereal/archives/portable_binary.hpp"
#include "cereal/types/string.hpp"

/**
 * @brief 定义消息内容的类型 
 */
enum class MessageContentType {
    MSG_EMPTY,
    MSG_TEXT,
    MSG_IMAGE, 
    MSG_FILE   
};

/**
 * @brief 消息发送者的信息 
 */
struct SenderInfo {
    std::string name;
    // ... 未来可以添加更多信息，如 ID, 头像等

    // 默认构造函数
    SenderInfo() : name("Default User") {}
    
    // 移动构造函数 [cite: 433]
    SenderInfo(SenderInfo&& other) noexcept : name(std::move(other.name)) {}

    // 移动赋值运算符 [cite: 434-444]
    SenderInfo& operator=(SenderInfo&& other) noexcept {
        if (this != &other) {
            name = std::move(other.name);
        }
        return *this;
    }
    
    // 阻止拷贝 (因为我们主要使用移动)
    SenderInfo(const SenderInfo&) = delete;
    SenderInfo& operator=(const SenderInfo&) = delete;

    // Cereal 序列化函数
    template <class Archive>
    void serialize(Archive& ar) {
        ar(name);
    }
};

/**
 * @brief 网络通信的消息对象 (私有消息格式)
 */
class Message {
public:
    SenderInfo sender;
    std::string content;
    std::time_t timestamp;
    MessageContentType content_type;

public:
    // 默认构造函数 
    Message() : timestamp(0), content_type(MessageContentType::MSG_EMPTY) {}

    Message(Message&& other) noexcept
        : sender(std::move(other.sender)),
          content(std::move(other.content)),
          timestamp(other.timestamp),
          content_type(other.content_type) {}

    Message& operator=(Message&& other) noexcept {
        if (this != &other) {
            sender = std::move(other.sender);
            content = std::move(other.content);
            timestamp = other.timestamp;
            content_type = other.content_type;
        }
        return *this;
    }

    // 阻止拷贝
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;

    // --- 序列化 / 反序列化 ---

    /**
     * @brief 将此 Message 对象序列化为二进制 string
     */
    std::string serialize() const;

    /**
     * @brief 从二进制 string 反序列化为 Message 对象
     */
    static Message deserialize(const std::string& data);

    // --- 工厂函数 (Helpers) ---

    /**
     * @brief 创建一个文本消息
     */
    static Message make_text(SenderInfo sender, std::string content);

    /**
     * @brief 创建一个空消息
     */
    static Message make_empty();

    // Cereal 序列化函数
    template <class Archive>
    void serialize(Archive& ar) {
        ar(sender, content, timestamp, content_type);
    }

private:
    // 私有构造函数，供工厂函数使用
    Message(SenderInfo sender, std::string content, std::time_t timestamp, MessageContentType type);
};