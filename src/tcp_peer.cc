#include "tcp_peer.h"
#include <iostream>
#include <stdexcept>
#include <system_error> // 用于 errno
#include <cstring>      // 用于 strerror, memset

// --- 包含 POSIX Socket 头文件 ---
#include <sys/socket.h>
#include <netinet/in.h> // for sockaddr_in
#include <arpa/inet.h>  // for inet_pton
#include <unistd.h>     // for read, write, close
#include <netdb.h>      // for gethostbyname
#include <fcntl.h>      // for fcntl (设置非阻塞)
#include <arpa/inet.h>  // for htonl, ntohl (网络字节序)

// --- 帮助函数 (健壮的 I/O) ---
// TCP 不能保证一次 read/write 能读/写完所有数据，必须循环

/**
 * @brief 健壮地写入 N 字节
 */
bool robust_write(int fd, const void* data, size_t count) {
    const char* buffer = static_cast<const char*>(data);
    size_t written = 0;
    while (written < count) {
        ssize_t result = write(fd, buffer + written, count - written);
        if (result <= 0) {
            // 0 表示连接关闭, -1 表示错误
            return false;
        }
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
        if (result <= 0) {
            // 0 表示连接关闭, -1 表示错误
            return false;
        }
        read_bytes += result;
    }
    return true;
}


// --- TcpPeer 构造/析构 ---

TcpPeer::TcpPeer() {
    // 构造函数什么都不做
}

TcpPeer::~TcpPeer() {
    close_connection();
}

// --- 公共 API 实现 ---

bool TcpPeer::connect_to(const std::string& ip, int port) {
    if (is_connected_) return true;

    // 1. 创建 socket
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "Error: socket creation failed. " << strerror(errno) << std::endl;
        return false;
    }

    // 2. 准备地址
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port); // 转换为网络字节序

    // 将 IP 字符串转换为网络地址
    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Error: Invalid IP address. " << ip << std::endl;
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    // 3. 连接
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error: Connection failed. " << strerror(errno) << std::endl;
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

    // 1. 创建监听 socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "Error: socket creation failed. " << strerror(errno) << std::endl;
        return false;
    }

    // 允许地址重用 (非常重要, 否则重启程序会失败)
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. 准备地址
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // 监听所有网卡
    server_addr.sin_port = htons(port);

    // 3. 绑定
    if (::bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error: bind failed. " << strerror(errno) << std::endl;
        ::close(listen_fd);
        return false;
    }

    // 4. 监听
    if (::listen(listen_fd, 1) < 0) { // 只接受1个连接
        std::cerr << "Error: listen failed. " << strerror(errno) << std::endl;
        ::close(listen_fd);
        return false;
    }

    std::cout << "[Peer] Listening on port " << port << ". Waiting for connection..." << std::endl;

    // 5. 接受连接 (阻塞)
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    socket_fd_ = ::accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);

    if (socket_fd_ < 0) {
        std::cerr << "Error: accept failed. " << strerror(errno) << std::endl;
        ::close(listen_fd);
        return false;
    }

    // 接受连接后，关闭监听 socket
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
    if (stop_flag_ || !is_connected_) return;

    std::cout << "[Peer] Closing connection..." << std::endl;
    stop_flag_ = true;
    is_connected_ = false;

    // 1. 关闭 socket
    if (socket_fd_ != -1) {
        // 立即终止读写，这将使 read/write 立即返回错误
        shutdown(socket_fd_, SHUT_RDWR);
        ::close(socket_fd_);
        socket_fd_ = -1;
    }

    // 2. 唤醒所有在队列上等待的线程
    send_queue_.notify_all();
    recv_queue_.notify_all();

    // 3. 等待线程退出
    if (sender_thread_.joinable()) {
        sender_thread_.join();
    }
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
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
    // 启动两个线程，并分离 (detach) 它们
    // 或者使用 std::move 转移所有权
    sender_thread_ = std::thread(&TcpPeer::sender_loop, this);
    receiver_thread_ = std::thread(&TcpPeer::receiver_loop, this);
}

// --- 线程循环 ---

void TcpPeer::sender_loop() {
    while (!stop_flag_) {
        Message msg;
        // 1. 阻塞式地从发送队列获取消息
        if (!send_queue_.wait_and_pop(msg)) {
            if (stop_flag_) break; // 如果被唤醒是因为停止，则退出
            continue;
        }

        // 2. 序列化
        std::string data = msg.serialize();
        if (data.empty()) continue;

        // 3. 准备长度前缀 (网络字节序)
        uint32_t len = data.length();
        uint32_t len_net = htonl(len); // Host-To-Network-Long

        // 4. 发送长度
        if (!robust_write(socket_fd_, &len_net, sizeof(len_net))) {
            std::cerr << "[Sender] Connection lost (write length)." << std::endl;
            close_connection();
            break;
        }

        // 5. 发送数据
        if (!robust_write(socket_fd_, data.data(), len)) {
            std::cerr << "[Sender] Connection lost (write data)." << std::endl;
            close_connection();
            break;
        }
    }
    std::cout << "[Sender] Thread exiting." << std::endl;
}

void TcpPeer::receiver_loop() {
    while (!stop_flag_) {
        // 1. 读取长度前缀
        uint32_t len_net = 0;
        if (!robust_read(socket_fd_, &len_net, sizeof(len_net))) {
            if (!stop_flag_) {
                std::cerr << "[Receiver] Connection lost (read length)." << std::endl;
            }
            close_connection(); // 对方关闭了连接
            break;
        }

        // 2. 转换为主机字节序
        uint32_t len = ntohl(len_net); // Network-To-Host-Long
        
        if (len > 10 * 1024 * 1024) { // 10MB 限制，防止恶意包
             std::cerr << "[Receiver] Error: Message too large: " << len << std::endl;
             close_connection();
             break;
        }
        if (len == 0) continue; // 可能是空消息

        // 3. 读取数据
        std::string buffer(len, '\0');
        if (!robust_read(socket_fd_, buffer.data(), len)) {
             std::cerr << "[Receiver] Connection lost (read data)." << std::endl;
             close_connection();
             break;
        }

        // 4. 反序列化
        try {
            Message msg = Message::deserialize(buffer);
            recv_queue_.push(std::move(msg));
        } catch (const std::exception& e) {
            std::cerr << "[Receiver] Deserialization error: " << e.what() << std::endl;
            // 继续接收下一条，不中断连接
        }
    }
    std::cout << "[Receiver] Thread exiting." << std::endl;
}