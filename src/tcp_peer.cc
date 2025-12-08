#include "tcp_peer.h"
#include <iostream>
#include <stdexcept>
#include <system_error> 
#include <cstring>      

#include <sys/socket.h>
#include <netinet/in.h> 
#include <arpa/inet.h>  
#include <unistd.h>     
#include <netdb.h>      
#include <fcntl.h>      
#include <arpa/inet.h>  

// --- 帮助函数 (健壮的 I/O) ---
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

// --- TcpPeer 构造/析构 ---

TcpPeer::TcpPeer() {
    // 构造函数什么都不做
}

TcpPeer::~TcpPeer() {
    // 1. 先尝试关闭连接，触发线程退出
    close_connection();

    // 2. [关键修复] 在析构函数中安全地回收所有线程
    // 只要线程还 joinable，就必须 join，否则 std::thread 析构会触发 terminate
    if (sender_thread_.joinable()) {
        // 防止析构函数在 sender_thread 中被调用的极罕见情况（通常不会）
        if (std::this_thread::get_id() != sender_thread_.get_id()) {
            sender_thread_.join();
        } else {
            sender_thread_.detach(); 
        }
    }

    if (receiver_thread_.joinable()) {
        if (std::this_thread::get_id() != receiver_thread_.get_id()) {
            receiver_thread_.join();
        } else {
            receiver_thread_.detach();
        }
    }
}

// --- 公共 API 实现 ---

bool TcpPeer::connect_to(const std::string& ip, int port) {
    if (is_connected_) return true;

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "Error: socket creation failed. " << strerror(errno) << std::endl;
        return false;
    }

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Error: Invalid IP address. " << ip << std::endl;
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        // std::cerr << "Error: Connection failed. " << strerror(errno) << std::endl;
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    std::cout << "[Peer] Successfully connected to " << ip << ":" << port << std::endl;
    is_connected_ = true;
    stop_flag_ = false;
    start_threads();
    return true;
}

bool TcpPeer::listen_on(int port) {
    if (is_connected_) return true;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "Error: socket creation failed. " << strerror(errno) << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    server_addr.sin_port = htons(port);

    if (::bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error: bind failed. " << strerror(errno) << std::endl;
        ::close(listen_fd);
        return false;
    }

    if (::listen(listen_fd, 1) < 0) { 
        std::cerr << "Error: listen failed. " << strerror(errno) << std::endl;
        ::close(listen_fd);
        return false;
    }

    std::cout << "[Peer] Listening on port " << port << ". Waiting for connection..." << std::endl;

    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    socket_fd_ = ::accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);

    if (socket_fd_ < 0) {
        std::cerr << "Error: accept failed. " << strerror(errno) << std::endl;
        ::close(listen_fd);
        return false;
    }

    ::close(listen_fd);

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    std::cout << "[Peer] Connection accepted from " << client_ip << std::endl;

    is_connected_ = true;
    stop_flag_ = false;
    start_threads();
    return true;
}

void TcpPeer::close_connection() {
    // 使用 CAS 防止多线程重复进入
    bool expected = false;
    if (!stop_flag_.compare_exchange_strong(expected, true)) {
        return; 
    }

    std::cout << "[Peer] Closing connection..." << std::endl;
    is_connected_ = false;

    // 1. 关闭 socket 唤醒阻塞的 I/O
    if (socket_fd_ != -1) {
        shutdown(socket_fd_, SHUT_RDWR);
        ::close(socket_fd_);
        socket_fd_ = -1;
    }

    // 2. 唤醒等待队列
    send_queue_.notify_all();
    recv_queue_.notify_all();

    // 3. 尝试 Join 线程 (但跳过自己)
    std::thread::id this_id = std::this_thread::get_id();

    // 处理发送线程
    if (sender_thread_.joinable()) {
        if (sender_thread_.get_id() != this_id) {
            sender_thread_.join();
        }
        // 如果是自己，不 join，也不 detach！留给析构函数去处理。
    }

    // 处理接收线程
    if (receiver_thread_.joinable()) {
        if (receiver_thread_.get_id() != this_id) {
            receiver_thread_.join();
        }
        // 同上，如果是自己，不处理。
    }
}

void TcpPeer::send_message(Message&& msg) {
    if (!is_connected_ || stop_flag_) return;
    send_queue_.push(std::move(msg));
}

bool TcpPeer::try_recv_message(Message& msg_out) {
    if (!is_connected_) return false;
    return recv_queue_.try_pop(msg_out);
}

bool TcpPeer::is_connected() const {
    return is_connected_;
}

// --- 内部辅助函数 ---

void TcpPeer::start_threads() {
    sender_thread_ = std::thread(&TcpPeer::sender_loop, this);
    receiver_thread_ = std::thread(&TcpPeer::receiver_loop, this);
}

// --- 线程循环 ---

void TcpPeer::sender_loop() {
    while (!stop_flag_) {
        Message msg;
        if (!send_queue_.wait_and_pop(msg)) {
            if (stop_flag_) break; 
            continue;
        }

        std::string data = msg.serialize();
        if (data.empty()) continue;

        uint32_t len = data.length();
        uint32_t len_net = htonl(len); 

        if (!robust_write(socket_fd_, &len_net, sizeof(len_net))) {
            // std::cerr << "[Sender] Connection lost (write length)." << std::endl;
            close_connection();
            break;
        }

        if (!robust_write(socket_fd_, data.data(), len)) {
            // std::cerr << "[Sender] Connection lost (write data)." << std::endl;
            close_connection();
            break;
        }
    }
}

void TcpPeer::receiver_loop() {
    while (!stop_flag_) {
        uint32_t len_net = 0;
        if (!robust_read(socket_fd_, &len_net, sizeof(len_net))) {
            if (!stop_flag_) {
                std::cerr << "[Receiver] Connection lost (read length)." << std::endl;
            }
            close_connection(); 
            break;
        }

        uint32_t len = ntohl(len_net); 
        
        if (len > 20 * 1024 * 1024) { 
             std::cerr << "[Receiver] Error: Message too large: " << len << std::endl;
             close_connection();
             break;
        }
        if (len == 0) continue; 

        std::string buffer(len, '\0');
        if (!robust_read(socket_fd_, buffer.data(), len)) {
             std::cerr << "[Receiver] Connection lost (read data)." << std::endl;
             close_connection();
             break;
        }

        try {
            Message msg = Message::deserialize(buffer);
            recv_queue_.push(std::move(msg));
        } catch (const std::exception& e) {
            std::cerr << "[Receiver] Deserialization error: " << e.what() << std::endl;
        }
    }
}