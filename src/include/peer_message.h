#pragma once

#include <string>
#include <vector> // 新增
#include <ctime>
#include <sstream>

// 包含 cereal 序列化库的头文件
#include "cereal/archives/portable_binary.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp" // 新增: 序列化 std::vector

/**
 * @brief 消息的顶层类型，用于区分不同功能
 */
enum class MessageType {
    MSG_LOGIN_REQUEST,      // 客户端 -> 服务器 (请求登录，携带用户名)
    MSG_CHAT,               // 客户端 -> 服务器 / 服务器 -> 客户端 (聊天内容)
    MSG_USER_JOIN_BCAST,    // 服务器 -> 客户端 (广播：用户加入)
    MSG_USER_EXIT_BCAST,    // 服务器 -> 客户端 (广播：用户离开)
    MSG_USER_LIST_BCAST,    // 服务器 -> 客户端 (广播：当前用户列表)
    MSG_SYS_ANNOUNCE_BCAST  // 服务器 -> 客户端 (广播：系统公告)
};

/**
 * @brief 聊天消息的模式 (群聊或私聊)
 */
enum class ChatMode {
    MODE_GROUP,
    MODE_PRIVATE
};

/**
 * @brief 消息发送者的信息
 */
struct SenderInfo {
    std::string name;

    SenderInfo() : name("Default User") {}
    
    // (移动构造函数、移动赋值、阻止拷贝... 假设与 Lab 1 相同)
    SenderInfo(SenderInfo&& other) noexcept : name(std::move(other.name)) {}
    SenderInfo& operator=(SenderInfo&& other) noexcept {
        if (this != &other) { name = std::move(other.name); }
        return *this;
    }
    SenderInfo(const SenderInfo&) = delete;
    SenderInfo& operator=(const SenderInfo&) = delete;

    // Cereal 序列化 (使用 save/load 修正 const 问题)
    template <class Archive>
    void save(Archive& ar) const {
        ar(name);
    }
    template <class Archive>
    void load(Archive& ar) {
        ar(name);
    }
};

/**
 * @brief 网络通信的消息对象
 */
class Message {
public:
    // --- 消息成员 ---
    SenderInfo sender;            // 发送者信息
    std::string content;          // 消息内容 (聊天、公告等)
    std::time_t timestamp;        // 时间戳
    
    // --- Lab 2 新增成员 ---
    MessageType type;             // 消息的大类型
    ChatMode chat_mode;           // 聊天模式 (群聊/私聊)
    std::string target_user;      // 私聊对象 (如果是群聊则为空)
    std::vector<std::string> user_list; // 用于服务器广播用户列表

public:
    // 默认构造函数 
    Message() : timestamp(0), 
                type(MessageType::MSG_CHAT), 
                chat_mode(ChatMode::MODE_GROUP) {}

    Message(Message&& other) noexcept
        : sender(std::move(other.sender)),
          content(std::move(other.content)),
          timestamp(other.timestamp),
          type(other.type),
          chat_mode(other.chat_mode),
          target_user(std::move(other.target_user)),
          user_list(std::move(other.user_list)) {}

    Message& operator=(Message&& other) noexcept {
        if (this != &other) {
            sender = std::move(other.sender);
            content = std::move(other.content);
            timestamp = other.timestamp;
            type = other.type;
            chat_mode = other.chat_mode;
            target_user = std::move(other.target_user);
            user_list = std::move(other.user_list);
        }
        return *this;
    }
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;

    // --- 序列化 / 反序列化 (接口不变) ---
    std::string serialize() const;
    static Message deserialize(const std::string& data);

    // --- Cereal 序列化 (修正 Lab 1 的 const 错误) ---
    // Cereal 会自动调用 'save' (用于 const 对象)
    template <class Archive>
    void save(Archive& ar) const {
        ar(sender, content, timestamp, type, chat_mode, target_user, user_list);
    }
    // Cereal 会自动调用 'load' (用于非 const 对象)
    template <class Archive>
    void load(Archive& ar) {
        ar(sender, content, timestamp, type, chat_mode, target_user, user_list);
    }

    // --- 工厂函数 (Helpers) ---
    static Message make_empty();
    static Message make_login_request(SenderInfo sender);
    static Message make_group_chat(SenderInfo sender, std::string content);
    static Message make_private_chat(SenderInfo sender, std::string target, std::string content);
    static Message make_system_announce(std::string content);
    static Message make_user_broadcast(MessageType type, SenderInfo user, std::vector<std::string> user_list);
};