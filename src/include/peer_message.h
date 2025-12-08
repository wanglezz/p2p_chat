#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <sstream>

// 引入 cereal 库
#include "cereal/archives/portable_binary.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp" 

/**
 * @brief 消息类型枚举
 */
enum class MessageType {
    MSG_LOGIN_REQUEST,      // 登录请求
    MSG_CHAT,               // 聊天消息
    MSG_USER_JOIN_BCAST,    // 用户加入广播
    MSG_USER_EXIT_BCAST,    // 用户离开广播
    MSG_USER_LIST_BCAST,    // 用户列表广播
    MSG_SYS_ANNOUNCE_BCAST, // 系统公告
    
    // --- 文件传输 (Relay 模式) ---
    MSG_FILE_HEADER,        // 文件头 (文件名, 大小)
    MSG_FILE_DATA,          // 文件内容块

    // --- P2P 直连信令 (Lab 3 进阶) ---
    MSG_P2P_TRANS_REQ       // 请求建立 P2P 连接 (携带 IP 和 Port)
};

/**
 * @brief 聊天模式
 */
enum class ChatMode {
    MODE_GROUP,   // 群聊
    MODE_PRIVATE  // 私聊
};

/**
 * @brief 发送者信息
 */
struct SenderInfo {
    std::string name;

    SenderInfo() : name("Default User") {}
    
    // 移动语义支持
    SenderInfo(SenderInfo&& other) noexcept : name(std::move(other.name)) {}
    SenderInfo& operator=(SenderInfo&& other) noexcept {
        if (this != &other) { name = std::move(other.name); }
        return *this;
    }
    // 禁用拷贝
    SenderInfo(const SenderInfo&) = delete;
    SenderInfo& operator=(const SenderInfo&) = delete;

    template <class Archive>
    void save(Archive& ar) const { ar(name); }
    template <class Archive>
    void load(Archive& ar) { ar(name); }
};

/**
 * @brief 通用消息类
 */
class Message {
public:
    SenderInfo sender;            
    std::string content;          
    std::time_t timestamp;        
    
    MessageType type;             
    ChatMode chat_mode;           
    std::string target_user;      // 目标用户 (空字符串代表群发)
    std::vector<std::string> user_list; 

    // --- 文件信息 ---
    std::string file_name;        
    uint64_t file_size;           

    // --- P2P 直连信息 ---
    std::string p2p_server_ip;    // 发送方监听的 IP
    int p2p_server_port;          // 发送方监听的 Port

public:
    Message() : timestamp(0), type(MessageType::MSG_CHAT), chat_mode(ChatMode::MODE_GROUP), 
                file_size(0), p2p_server_port(0) {}

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
          file_size(other.file_size),
          p2p_server_ip(std::move(other.p2p_server_ip)),
          p2p_server_port(other.p2p_server_port) {}

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
            p2p_server_ip = std::move(other.p2p_server_ip);
            p2p_server_port = other.p2p_server_port;
        }
        return *this;
    }
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;

    std::string serialize() const;
    static Message deserialize(const std::string& data);

    // 序列化包含所有新字段
    template <class Archive>
    void save(Archive& ar) const {
        ar(sender, content, timestamp, type, chat_mode, target_user, user_list, 
           file_name, file_size, p2p_server_ip, p2p_server_port);
    }
    template <class Archive>
    void load(Archive& ar) {
        ar(sender, content, timestamp, type, chat_mode, target_user, user_list, 
           file_name, file_size, p2p_server_ip, p2p_server_port);
    }

    // --- 工厂函数 ---
    static Message make_empty();
    static Message make_login_request(SenderInfo sender);
    static Message make_group_chat(SenderInfo sender, std::string content);
    static Message make_private_chat(SenderInfo sender, std::string target, std::string content);
    static Message make_system_announce(std::string content);
    static Message make_user_broadcast(MessageType type, SenderInfo user, std::vector<std::string> user_list);

    // 文件相关
    static Message make_file_header(SenderInfo sender, std::string target, std::string filename, uint64_t size);
    static Message make_file_chunk(SenderInfo sender, std::string target, std::string data);
    
    // P2P 相关
    static Message make_p2p_request(SenderInfo sender, std::string target, std::string filename, uint64_t size, std::string ip, int port);
};