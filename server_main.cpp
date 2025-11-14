#include <iostream>
#include <string>
#include <vector>
#include <map>              // 用于存储 "用户名 -> socket" 的映射
#include <thread>           
#include <mutex>            // 用于保护共享的 map
#include <stdexcept>
#include <system_error>
#include <cstring>

// --- POSIX Socket 头文件 ---
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "peer_message.h"

// --- 全局共享状态 (必须用互斥锁保护) ---
// 映射：用户名 -> socket 文件描述符
std::map<std::string, int> g_clients;
// 用于保护 g_clients 的互斥锁
std::mutex g_clients_mutex;
// ---

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
 * @brief 从全局 map 中获取当前在线用户列表 (线程安全)
 */
std::vector<std::string> GetOnlineUserList() {
    std::vector<std::string> user_list;
    // 锁住互斥锁，防止在迭代时 map 被修改
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    
    for (const auto& pair : g_clients) {
        user_list.push_back(pair.first);
    }
    return user_list;
}

/**
 * @brief 将一条消息广播给所有连接的客户端 (线程安全)
 */
void BroadcastMessage(const Message& msg) {
    // 序列化消息一次，供所有客户端使用
    std::string data = msg.serialize();
    uint32_t len = data.length();
    uint32_t len_net = htonl(len);

    // 锁住互斥锁，安全地迭代 g_clients
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    
    for (const auto& pair : g_clients) {
        int client_fd = pair.second;
        // 1. 发送长度前缀
        if (!robust_write(client_fd, &len_net, sizeof(len_net))) {
            std::cerr << "Broadcast: Failed to write len to " << pair.first << std::endl;
        }
        // 2. 发送数据
        if (!robust_write(client_fd, data.data(), len)) {
            std::cerr << "Broadcast: Failed to write data to " << pair.first << std::endl;
        }
    }
}

/**
 * @brief 处理单个客户端所有通信的函数 (在单独的线程中运行)
 */
void ClientHandler(int client_socket_fd) {
    std::string username;
    SenderInfo sender_info;

    try {
        // --- 1. 登录阶段 ---
        // 客户端连接后发送的第一条消息必须是登录请求
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
        sender_info = std::move(login_msg.sender); // 保存 SenderInfo 以备后用

        // --- 2. 注册用户 (线程安全) ---
        { // 互斥锁的单独作用域
            std::lock_guard<std::mutex> lock(g_clients_mutex);
            if (g_clients.count(username)) {
                // (可选: 发送一条错误消息回客户端)
                throw std::runtime_error("Username " + username + " already taken.");
            }
            g_clients[username] = client_socket_fd; // 注册用户
        }

        std::cout << "[Server] User '" << username << "' connected. Socket: " << client_socket_fd << std::endl;
        
        // --- 3. 广播用户加入 & 发送用户列表  ---
        std::vector<std::string> user_list = GetOnlineUserList();
        Message join_broadcast = Message::make_user_broadcast(
            MessageType::MSG_USER_JOIN_BCAST, 
            std::move(sender_info), // 发送者是刚加入的用户
            user_list
        );
        BroadcastMessage(join_broadcast);

        // --- 4. 消息循环 ---
        while (true) {
            uint32_t len_net;
            if (!robust_read(client_socket_fd, &len_net, sizeof(len_net))) {
                // 读取失败 = 客户端断开连接
                break;
            }
            uint32_t len = ntohl(len_net);

            if (len == 0 || len > 10 * 1024 * 1024) { // 10MB 限制
                std::cerr << "Invalid message length " << len << " from " << username << std::endl;
                continue; // 忽略此消息并继续
            }

            std::string buffer(len, '\0');
            if (!robust_read(client_socket_fd, buffer.data(), len)) {
                // 读取失败 = 客户端断开连接
                break;
            }

            Message msg = Message::deserialize(buffer);

            // --- 5. 消息转发逻辑 ---
            switch (msg.type) {
                case MessageType::MSG_CHAT:
                    if (msg.chat_mode == ChatMode::MODE_GROUP) {
                        // A. 群发
                        std::cout << "[Server] Group chat from '" << msg.sender.name << "': " << msg.content << std::endl;
                        BroadcastMessage(msg);
                    } 
                    else if (msg.chat_mode == ChatMode::MODE_PRIVATE) {
                        // B. 私聊
                        std::cout << "[Server] Private chat from '" << msg.sender.name << "' to '" << msg.target_user << "'" << std::endl;
                        
                        // 序列化一次
                        std::string data = msg.serialize();
                        uint32_t data_len = data.length();
                        uint32_t data_len_net = htonl(data_len);

                        std::lock_guard<std::mutex> lock(g_clients_mutex);
                        
                        // 1. 发给目标
                        if (g_clients.count(msg.target_user)) {
                            int target_fd = g_clients[msg.target_user];
                            robust_write(target_fd, &data_len_net, sizeof(data_len_net));
                            robust_write(target_fd, data.data(), data_len);
                        }
                        // 2. 也发给自己，作为 "已发送" 确认
                        robust_write(client_socket_fd, &data_len_net, sizeof(data_len_net));
                        robust_write(client_socket_fd, data.data(), data_len);
                    }
                    break;
                
                default:
                    std::cerr << "[Server] Unknown message type " << (int)msg.type << " from " << username << std::endl;
            }
        }
    
    } catch (const std::exception& e) {
        std::cerr << "[Handler Error] " << e.what() << std::endl;
    }

    // --- 6. 清理阶段 (客户端断开连接) ---
    std::cout << "[Server] User '" << username << "' disconnected." << std::endl;
    
    // 从全局 map 中移除 (线程安全)
    {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        g_clients.erase(username);
    }
    
    // 关闭 socket
    close(client_socket_fd);

    // 广播用户离开 [cite: 527]
    std::vector<std::string> user_list = GetOnlineUserList();
    SenderInfo exit_sender; // 创建一个临时的 SenderInfo
    exit_sender.name = username;
    Message exit_broadcast = Message::make_user_broadcast(
        MessageType::MSG_USER_EXIT_BCAST,
        std::move(exit_sender),
        user_list
    );
    BroadcastMessage(exit_broadcast);
    
    // 线程函数结束，此线程被销毁
}

/**
 * @brief 主线程：负责监听和接受新连接
 */
int main() {
    int port = 9001; // 硬编码端口号

    // --- 1. 创建监听 socket ---
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "Error: socket creation failed. " << strerror(errno) << std::endl;
        return 1;
    }

    // 允许地址重用 (非常重要)
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // --- 2. 准备地址并绑定 ---
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // 监听所有网卡
    server_addr.sin_port = htons(port);

    if (::bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error: bind failed. " << strerror(errno) << std::endl;
        ::close(listen_fd);
        return 1;
    }

    // --- 3. 监听 ---
    if (::listen(listen_fd, 10) < 0) { // 允许最多 10 个连接排队
        std::cerr << "Error: listen failed. " << strerror(errno) << std::endl;
        ::close(listen_fd);
        return 1;
    }

    std::cout << "[Server] ChatServer listening on port " << port << "..." << std::endl;

    // --- 4. 主循环：接受新连接  ---
    while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // 阻塞，直到有新客户端连接
        int client_socket_fd = ::accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_socket_fd < 0) {
            std::cerr << "Error: accept failed. " << strerror(errno) << std::endl;
            continue; // 继续下一个循环，而不是退出
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "[Server] Accepted new connection from " << client_ip << std::endl;

        // --- 5. 并发处理 ---
        // 为这个新客户端创建一个单独的线程来处理它
        // .detach() 使线程在后台运行，主线程不用管它
        try {
            std::thread(ClientHandler, client_socket_fd).detach();
        } catch (const std::system_error& e) {
            std::cerr << "Error: Failed to create thread. " << e.what() << std::endl;
            close(client_socket_fd);
        }
    }

    // (主循环永远不会退出，除非程序被终止)
    close(listen_fd);
    return 0;
}