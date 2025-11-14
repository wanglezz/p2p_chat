#include "peer_message.h"

// 包含实现所需的其他标准库
#include <sstream>      // 用于 std::stringstream
#include <utility>      // 用于 std::move
#include <stdexcept>    // 用于 std::exception
#include <ctime>        // 用于 std::time

/**
 * @brief 序列化
 * 它会自动调用 .h 文件中的 save() const
 */
std::string Message::serialize() const {
    std::stringstream ss;
    { 
        cereal::PortableBinaryOutputArchive oarchive(ss);
        oarchive(*this); // Cereal 会自动查找并调用 save() const
    }
    return ss.str();
}

/**
 * @brief 反序列化 (此函数实现与 Lab 1 相同)
 * 它会自动调用 .h 文件中的 load()
 */
Message Message::deserialize(const std::string& data) {
    std::stringstream ss(data);
    Message msg; 

    try {
        cereal::PortableBinaryInputArchive iarchive(ss);
        iarchive(msg); // Cereal 会自动查找并调用 load()
    } catch (const std::exception& e) {
        return Message::make_empty();
    }
    return msg; 
}

/**
 * @brief 创建一个空消息
 */
Message Message::make_empty() {
    return Message();
}

/**
 * @brief 创建一个登录请求消息
 */
Message Message::make_login_request(SenderInfo sender) {
    Message msg;
    msg.type = MessageType::MSG_LOGIN_REQUEST;
    msg.sender = std::move(sender);
    msg.timestamp = std::time(nullptr);
    return msg; // 依赖 C++ 的 move on return
}

/**
 * @brief 创建一个群聊消息
 */
Message Message::make_group_chat(SenderInfo sender, std::string content) {
    Message msg;
    msg.type = MessageType::MSG_CHAT;
    msg.chat_mode = ChatMode::MODE_GROUP;
    msg.sender = std::move(sender);
    msg.content = std::move(content);
    msg.timestamp = std::time(nullptr);
    return msg;
}

/**
 * @brief 创建一个私聊消息
 */
Message Message::make_private_chat(SenderInfo sender, std::string target, std::string content) {
    Message msg;
    msg.type = MessageType::MSG_CHAT;
    msg.chat_mode = ChatMode::MODE_PRIVATE;
    msg.sender = std::move(sender);
    msg.target_user = std::move(target);
    msg.content = std::move(content);
    msg.timestamp = std::time(nullptr);
    return msg;
}

/**
 * @brief 创建一个系统公告 (由服务器发送)
 */
Message Message::make_system_announce(std::string content) {
    Message msg;
    msg.type = MessageType::MSG_SYS_ANNOUNCE_BCAST;
    msg.sender.name = "System"; // 发送者固定为 "System"
    msg.content = std::move(content);
    msg.timestamp = std::time(nullptr);
    return msg;
}

/**
 * @brief 创建一个用户加入/离开/列表更新的广播
 */
Message Message::make_user_broadcast(MessageType type, SenderInfo user, std::vector<std::string> user_list) {
    Message msg;
    msg.type = type; // (应该是 MSG_USER_JOIN_BCAST, MSG_USER_EXIT_BCAST, 或 MSG_USER_LIST_BCAST)
    msg.sender = std::move(user);
    msg.user_list = std::move(user_list); // 附带最新的用户列表
    msg.timestamp = std::time(nullptr);
    return msg;
}