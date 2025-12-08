#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <sstream>

// 包含 cereal 序列化库的头文件
#include "cereal/archives/portable_binary.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp" 

/**
 * @brief 消息的顶层类型，用于区分不同功能
 */
enum class MessageType {
    MSG_LOGIN_REQUEST,      // 客户端 -> 服务器 (请求登录)
    MSG_CHAT,               // 聊天内容 (文本)
    MSG_USER_JOIN_BCAST,    // 广播：用户加入
    MSG_USER_EXIT_BCAST,    // 广播：用户离开
    MSG_USER_LIST_BCAST,    // 广播：用户列表
    MSG_SYS_ANNOUNCE_BCAST, // 广播：系统公告
    
    // --- Lab 3 新增 ---
    MSG_FILE_HEADER,        // 文件元数据 (文件名, 大小)
    MSG_FILE_DATA           // 文件数据块
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
    
    // 移动构造
    SenderInfo(SenderInfo&& other) noexcept : name(std::move(other.name)) {}
    SenderInfo& operator=(SenderInfo&& other) noexcept {
        if (this != &other) { name = std::move(other.name); }
        return *this;
    }
    // 禁用拷贝 (强制使用 std::move，提高效率并避免误用)
    SenderInfo(const SenderInfo&) = delete;
    SenderInfo& operator=(const SenderInfo&) = delete;

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
    // --- 基础成员 ---
    SenderInfo sender;            // 发送者
    std::string content;          // 文本内容 OR 文件数据块内容
    std::time_t timestamp;        // 时间戳
    
    MessageType type;             
    ChatMode chat_mode;           
    std::string target_user;      // 私聊/文件传输的目标对象 (空则为群发)
    std::vector<std::string> user_list; 

    // --- Lab 3 新增成员 ---
    std::string file_name;        // 文件名
    uint64_t file_size;           // 文件总大小

public:
    Message() : timestamp(0), 
                type(MessageType::MSG_CHAT), 
                chat_mode(ChatMode::MODE_GROUP),
                file_size(0) {}

    // 移动构造
    Message(Message&& other) noexcept
        : sender(std::move(other.sender)),
          content(std::move(other.content)),
          timestamp(other.timestamp),
          type(other.type),
          chat_mode(other.chat_mode),
          target_user(std::move(other.target_user)),
          user_list(std::move(other.user_list)),
          file_name(std::move(other.file_name)),
          file_size(other.file_size) {}

    Message& operator=(Message&& other) noexcept {
        if (this != &other) {
            sender = std::move(other.sender);
            content = std::move(other.content);
            timestamp = other.timestamp;
            type = other.type;
            chat_mode = other.chat_mode;
            target_user = std::move(other.target_user);
            user_list = std::move(other.user_list);
            file_name = std::move(other.file_name);
            file_size = other.file_size;
        }
        return *this;
    }
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;

    // --- 序列化接口 ---
    std::string serialize() const;
    static Message deserialize(const std::string& data);

    template <class Archive>
    void save(Archive& ar) const {
        ar(sender, content, timestamp, type, chat_mode, target_user, user_list, file_name, file_size);
    }
    template <class Archive>
    void load(Archive& ar) {
        ar(sender, content, timestamp, type, chat_mode, target_user, user_list, file_name, file_size);
    }

    // --- 工厂函数 ---
    static Message make_empty();
    static Message make_login_request(SenderInfo sender);
    static Message make_group_chat(SenderInfo sender, std::string content);
    static Message make_private_chat(SenderInfo sender, std::string target, std::string content);
    static Message make_system_announce(std::string content);
    static Message make_user_broadcast(MessageType type, SenderInfo user, std::vector<std::string> user_list);

    // Lab 3 新增
    static Message make_file_header(SenderInfo sender, std::string target, std::string filename, uint64_t size);
    static Message make_file_chunk(SenderInfo sender, std::string target, std::string data);
};