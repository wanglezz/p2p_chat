#include <iostream>
#include <string>
#include <vector>
#include <map>              
#include <thread>           
#include <mutex>            
#include <stdexcept>
#include <system_error>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "peer_message.h"

// --- 常量与全局 ---
constexpr int SERVER_PORT = 9001;
constexpr size_t MAX_MSG_SIZE = 20 * 1024 * 1024; // 20MB

std::map<std::string, int> g_clients;
std::mutex g_clients_mutex;

// --- Helper Functions ---
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

std::vector<std::string> GetOnlineUserList() {
    std::vector<std::string> user_list;
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    for (const auto& pair : g_clients) user_list.push_back(pair.first);
    return user_list;
}

void BroadcastMessage(const Message& msg) {
    std::string data = msg.serialize();
    uint32_t len = htonl(data.length());
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    for (const auto& pair : g_clients) {
        int fd = pair.second;
        robust_write(fd, &len, sizeof(len));
        robust_write(fd, data.data(), data.length());
    }
}

void SendSerializedMessageToUser(const std::string& target_user, const std::string& serialized_data) {
    uint32_t len = htonl(serialized_data.length());
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    if (g_clients.count(target_user)) {
        int fd = g_clients[target_user];
        robust_write(fd, &len, sizeof(len));
        robust_write(fd, serialized_data.data(), serialized_data.length());
    }
}

// --- Client Handler ---
void ClientHandler(int client_fd) {
    std::string username;
    try {
        // 1. Handshake
        uint32_t len_net;
        if (!robust_read(client_fd, &len_net, sizeof(len_net))) throw std::runtime_error("Read len failed");
        uint32_t len = ntohl(len_net);
        if (len == 0 || len > 1024) throw std::runtime_error("Invalid len");
        
        std::string buffer(len, '\0');
        if (!robust_read(client_fd, buffer.data(), len)) throw std::runtime_error("Read data failed");
        Message login_msg = Message::deserialize(buffer);
        username = login_msg.sender.name;
        
        {
            std::lock_guard<std::mutex> lock(g_clients_mutex);
            if (g_clients.count(username)) throw std::runtime_error("Username taken");
            g_clients[username] = client_fd;
        }
        std::cout << "[Server] " << username << " connected." << std::endl;
        BroadcastMessage(Message::make_user_broadcast(MessageType::MSG_USER_JOIN_BCAST, std::move(login_msg.sender), GetOnlineUserList()));

        // 2. Loop
        while (true) {
            if (!robust_read(client_fd, &len_net, sizeof(len_net))) break;
            len = ntohl(len_net);
            if (len == 0 || len > MAX_MSG_SIZE) break;

            buffer.resize(len);
            if (!robust_read(client_fd, buffer.data(), len)) break;

            Message msg = Message::deserialize(buffer);

            switch (msg.type) {
                case MessageType::MSG_CHAT:
                    if (msg.chat_mode == ChatMode::MODE_GROUP) {
                        BroadcastMessage(msg);
                    } else {
                        std::string data = msg.serialize();
                        SendSerializedMessageToUser(msg.target_user, data);
                        SendSerializedMessageToUser(username, data); // 回显
                    }
                    break;

                case MessageType::MSG_FILE_HEADER:
                case MessageType::MSG_FILE_DATA:
                    if (msg.chat_mode == ChatMode::MODE_GROUP) {
                        BroadcastMessage(msg);
                    } else {
                        // 私聊文件只转发，不回显(省流量)
                        std::string data = msg.serialize();
                        SendSerializedMessageToUser(msg.target_user, data);
                    }
                    break;

                case MessageType::MSG_P2P_TRANS_REQ:
                    // P2P 信令转发
                    std::cout << "[P2P Signal] " << msg.sender.name << " -> " << msg.target_user << std::endl;
                    SendSerializedMessageToUser(msg.target_user, msg.serialize());
                    break;

                default: break;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[Error] " << e.what() << std::endl;
    }

    std::cout << "[Server] " << username << " disconnected." << std::endl;
    { std::lock_guard<std::mutex> lock(g_clients_mutex); g_clients.erase(username); }
    close(client_fd);
    SenderInfo s; s.name = username;
    BroadcastMessage(Message::make_user_broadcast(MessageType::MSG_USER_EXIT_BCAST, std::move(s), GetOnlineUserList()));
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1; setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(SERVER_PORT); addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) return 1;
    if (listen(listen_fd, 10) < 0) return 1;

    std::cout << "Server listening on " << SERVER_PORT << std::endl;
    while (true) {
        sockaddr_in client_addr; socklen_t len = sizeof(client_addr);
        int fd = accept(listen_fd, (sockaddr*)&client_addr, &len);
        if (fd >= 0) std::thread(ClientHandler, fd).detach();
    }
    return 0;
}