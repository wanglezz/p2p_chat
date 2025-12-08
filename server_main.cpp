#include <iostream>
#include <string>
#include <vector>
#include <map>              
#include <thread>           
#include <mutex>            
#include <stdexcept>
#include <system_error>
#include <cstring>

// --- POSIX Socket 头文件 ---
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "peer_message.h"

// --- 全局常量与定义 ---
constexpr int SERVER_PORT = 9001;           ///< 服务器监听端口
constexpr size_t MAX_MSG_SIZE = 10 * 1024 * 1024; ///< 最大消息大小 (10MB)

// --- 全局共享状态 (需互斥锁保护) ---
std::map<std::string, int> g_clients;       ///< 用户名 -> socket 映射
std::mutex g_clients_mutex;                 ///< 保护 g_clients 的互斥锁

/**
 * @brief 健壮地写入 N 字节
 */
bool robust_write(int fd, const void* data, size_t count) {
    const char* buffer = static_cast<const char*>(data);
    size_t written = 0;
    while (written < count) {
        ssize_t result = write(fd, buffer + written, count - written);
        if (result <= 0) return false;
        written += result;
    }
    return true;
}

/**
 * @brief 健壮地读取 N 字节
 */
bool robust_read(int fd, void* data, size_t count) {
    char* buffer = static_cast<char*>(data);
    size_t read_bytes = 0;
    while (read_bytes < count) {
        ssize_t result = read(fd, buffer + read_bytes, count - read_bytes);
        if (result <= 0) return false;
        read_bytes += result;
    }
    return true;
}

/**
 * @brief 获取当前在线用户列表 (线程安全)
 */
std::vector<std::string> GetOnlineUserList() {
    std::vector<std::string> user_list;
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    
    for (const auto& pair : g_clients) {
        user_list.push_back(pair.first);
    }
    return user_list;
}

/**
 * @brief 广播消息给所有客户端 (线程安全)
 */
void BroadcastMessage(const Message& msg) {
    std::string data = msg.serialize();
    uint32_t len = data.length();
    uint32_t len_net = htonl(len);

    std::lock_guard<std::mutex> lock(g_clients_mutex);
    
    for (const auto& pair : g_clients) {
        int client_fd = pair.second;
        if (!robust_write(client_fd, &len_net, sizeof(len_net))) {
            std::cerr << "[Warning] Broadcast write len failed for " << pair.first << std::endl;
        }
        if (!robust_write(client_fd, data.data(), len)) {
            std::cerr << "[Warning] Broadcast write data failed for " << pair.first << std::endl;
        }
    }
}

/**
 * @brief 辅助函数：将序列化后的消息发送给指定用户
 */
void SendSerializedMessageToUser(const std::string& target_user, const std::string& serialized_data) {
    uint32_t len = serialized_data.length();
    uint32_t len_net = htonl(len);

    std::lock_guard<std::mutex> lock(g_clients_mutex);
    if (g_clients.count(target_user)) {
        int fd = g_clients[target_user];
        robust_write(fd, &len_net, sizeof(len_net));
        robust_write(fd, serialized_data.data(), len);
    }
}

/**
 * @brief 客户端处理线程函数
 */
void ClientHandler(int client_socket_fd) {
    std::string username;
    SenderInfo sender_info;

    try {
        // --- 1. 登录阶段 ---
        uint32_t len_net_login;
        if (!robust_read(client_socket_fd, &len_net_login, sizeof(len_net_login))) {
            throw std::runtime_error("Failed to read login msg len");
        }
        uint32_t len_login = ntohl(len_net_login);
        if (len_login == 0 || len_login > 1024) {
            throw std::runtime_error("Invalid login msg len");
        }
        
        std::string login_buffer(len_login, '\0');
        if (!robust_read(client_socket_fd, login_buffer.data(), len_login)) {
            throw std::runtime_error("Failed to read login msg data");
        }

        Message login_msg = Message::deserialize(login_buffer);
        if (login_msg.type != MessageType::MSG_LOGIN_REQUEST) {
            throw std::runtime_error("First message was not LOGIN_REQUEST");
        }
        
        username = login_msg.sender.name;
        sender_info = std::move(login_msg.sender); 

        // 注册用户
        { 
            std::lock_guard<std::mutex> lock(g_clients_mutex);
            if (g_clients.count(username)) {
                throw std::runtime_error("Username " + username + " already taken.");
            }
            g_clients[username] = client_socket_fd; 
        }

        std::cout << "[Server] User '" << username << "' connected." << std::endl;
        
        // 广播用户加入
        std::vector<std::string> user_list = GetOnlineUserList();
        Message join_broadcast = Message::make_user_broadcast(
            MessageType::MSG_USER_JOIN_BCAST, 
            std::move(sender_info), 
            user_list
        );
        BroadcastMessage(join_broadcast);

        // --- 2. 消息处理循环 ---
        while (true) {
            // 读取长度
            uint32_t len_net;
            if (!robust_read(client_socket_fd, &len_net, sizeof(len_net))) break;
            uint32_t len = ntohl(len_net);

            if (len == 0 || len > MAX_MSG_SIZE) { 
                std::cerr << "[Error] Invalid message length " << len << " from " << username << std::endl;
                // 这里选择断开连接，防止异常数据流
                break; 
            }

            // 读取内容
            std::string buffer(len, '\0');
            if (!robust_read(client_socket_fd, buffer.data(), len)) break;

            Message msg = Message::deserialize(buffer);

            // --- 消息分发逻辑 ---
            switch (msg.type) {
                case MessageType::MSG_CHAT:
                    if (msg.chat_mode == ChatMode::MODE_GROUP) {
                        // 群聊广播
                        std::cout << "[Chat] Group: " << msg.sender.name << " -> All" << std::endl;
                        BroadcastMessage(msg);
                    } 
                    else if (msg.chat_mode == ChatMode::MODE_PRIVATE) {
                        // 私聊转发
                        std::cout << "[Chat] Private: " << msg.sender.name << " -> " << msg.target_user << std::endl;
                        
                        std::string data = msg.serialize();
                        // 1. 发给目标
                        SendSerializedMessageToUser(msg.target_user, data);
                        // 2. 发给自己 (回显)
                        SendSerializedMessageToUser(username, data);
                    }
                    break;
                
                // --- Lab 3 文件传输处理 ---
                case MessageType::MSG_FILE_HEADER:
                case MessageType::MSG_FILE_DATA: 
                {
                    if (msg.chat_mode == ChatMode::MODE_GROUP) {
                        if (msg.type == MessageType::MSG_FILE_HEADER) {
                            std::cout << "[File] Group Broadcast: " << msg.sender.name 
                                    << " -> All (" << msg.file_name << ")" << std::endl;
                        }
                        BroadcastMessage(msg);
                    }
                    // 文件传输视为私聊的一种特殊形式，直接转发
                    if (msg.type == MessageType::MSG_FILE_HEADER) {
                        std::cout << "[File] Header: " << msg.sender.name << " -> " << msg.target_user 
                                  << " (" << msg.file_name << ")" << std::endl;
                    } 
                    std::string data = msg.serialize();
                    SendSerializedMessageToUser(msg.target_user, data);
                    // 注意：文件数据通常不回显给自己，节省带宽
                    break;
                }

                default:
                    std::cerr << "[Warning] Unknown message type from " << username << std::endl;
            }
        }
    
    } catch (const std::exception& e) {
        std::cerr << "[Handler Error] " << e.what() << std::endl;
    }

    // --- 3. 清理 ---
    std::cout << "[Server] User '" << username << "' disconnected." << std::endl;
    {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        g_clients.erase(username);
    }
    close(client_socket_fd);

    // 广播用户离开
    std::vector<std::string> user_list = GetOnlineUserList();
    SenderInfo exit_sender; 
    exit_sender.name = username;
    Message exit_broadcast = Message::make_user_broadcast(
        MessageType::MSG_USER_EXIT_BCAST,
        std::move(exit_sender),
        user_list
    );
    BroadcastMessage(exit_broadcast);
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "Error: socket creation failed. " << strerror(errno) << std::endl;
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    server_addr.sin_port = htons(SERVER_PORT);

    if (::bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error: bind failed. " << strerror(errno) << std::endl;
        close(listen_fd);
        return 1;
    }

    if (::listen(listen_fd, 10) < 0) { 
        std::cerr << "Error: listen failed. " << strerror(errno) << std::endl;
        close(listen_fd);
        return 1;
    }

    std::cout << "[Server] ChatServer listening on port " << SERVER_PORT << "..." << std::endl;

    while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket_fd = ::accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_socket_fd < 0) {
            std::cerr << "Error: accept failed. " << strerror(errno) << std::endl;
            continue; 
        }

        try {
            std::thread(ClientHandler, client_socket_fd).detach();
        } catch (const std::system_error& e) {
            std::cerr << "Error: Failed to create thread. " << e.what() << std::endl;
            close(client_socket_fd);
        }
    }
    close(listen_fd);
    return 0;
}