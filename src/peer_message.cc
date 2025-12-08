#include "peer_message.h"
#include <sstream>      
#include <utility>      
#include <stdexcept>    
#include <ctime>        

std::string Message::serialize() const {
    std::stringstream ss;
    { cereal::PortableBinaryOutputArchive oarchive(ss); oarchive(*this); }
    return ss.str();
}

Message Message::deserialize(const std::string& data) {
    std::stringstream ss(data);
    Message msg; 
    try { cereal::PortableBinaryInputArchive iarchive(ss); iarchive(msg); } 
    catch (...) { return Message::make_empty(); }
    return msg; 
}

Message Message::make_empty() { return Message(); }

Message Message::make_login_request(SenderInfo sender) {
    Message msg;
    msg.type = MessageType::MSG_LOGIN_REQUEST;
    msg.sender = std::move(sender);
    msg.timestamp = std::time(nullptr);
    return msg; 
}

Message Message::make_group_chat(SenderInfo sender, std::string content) {
    Message msg;
    msg.type = MessageType::MSG_CHAT;
    msg.chat_mode = ChatMode::MODE_GROUP;
    msg.sender = std::move(sender);
    msg.content = std::move(content);
    msg.timestamp = std::time(nullptr);
    return msg;
}

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

Message Message::make_system_announce(std::string content) {
    Message msg;
    msg.type = MessageType::MSG_SYS_ANNOUNCE_BCAST;
    msg.sender.name = "System"; 
    msg.content = std::move(content);
    msg.timestamp = std::time(nullptr);
    return msg;
}

Message Message::make_user_broadcast(MessageType type, SenderInfo user, std::vector<std::string> user_list) {
    Message msg;
    msg.type = type; 
    msg.sender = std::move(user);
    msg.user_list = std::move(user_list); 
    msg.timestamp = std::time(nullptr);
    return msg;
}

// --- Lab 3 文件与 P2P ---

Message Message::make_file_header(SenderInfo sender, std::string target, std::string filename, uint64_t size) {
    Message msg;
    msg.type = MessageType::MSG_FILE_HEADER;
    msg.chat_mode = target.empty() ? ChatMode::MODE_GROUP : ChatMode::MODE_PRIVATE;
    msg.sender = std::move(sender);
    msg.target_user = std::move(target);
    msg.file_name = std::move(filename);
    msg.file_size = size;
    msg.timestamp = std::time(nullptr);
    return msg;
}

Message Message::make_file_chunk(SenderInfo sender, std::string target, std::string data) {
    Message msg;
    msg.type = MessageType::MSG_FILE_DATA;
    msg.chat_mode = target.empty() ? ChatMode::MODE_GROUP : ChatMode::MODE_PRIVATE;
    msg.sender = std::move(sender);
    msg.target_user = std::move(target);
    msg.content = std::move(data);
    msg.timestamp = std::time(nullptr);
    return msg;
}

Message Message::make_p2p_request(SenderInfo sender, std::string target, std::string filename, uint64_t size, std::string ip, int port) {
    Message msg;
    msg.type = MessageType::MSG_P2P_TRANS_REQ;
    msg.chat_mode = ChatMode::MODE_PRIVATE; // P2P 通常是点对点
    msg.sender = std::move(sender);
    msg.target_user = std::move(target);
    msg.file_name = std::move(filename);
    msg.file_size = size;
    msg.p2p_server_ip = std::move(ip);
    msg.p2p_server_port = port;
    msg.timestamp = std::time(nullptr);
    return msg;
}