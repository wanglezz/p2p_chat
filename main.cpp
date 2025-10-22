#include "tcp_peer.h"
#include "peer_message.h"

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <memory>

// (假设你有一个 SenderInfo，我们先硬编码一个)
SenderInfo g_my_info;


/**
 * @brief 运行一个单独的线程来读取标准输入
 */
void input_loop(TcpPeer& peer) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "/quit") {
            peer.close_connection();
            break;
        }
        if (line.empty()) continue;

        // 创建一个 SenderInfo 的"副本"
        // 注意：SenderInfo 是 move-only 的，所以我们必须
        // 每次都创建一个新的，或者重新设计 SenderInfo 使其可拷贝
        // 这里我们假设可以为 g_my_info 赋值
        SenderInfo sender;
        sender.name = g_my_info.name; 

        Message msg = Message::make_text(std::move(sender), line);
        peer.send_message(std::move(msg));
    }
    std::cout << "[Input] Thread exiting." << std::endl;
}

/**
 * @brief 运行主聊天循环
 */
void run_chat_loop(TcpPeer& peer) {
    // 启动一个单独的线程来处理标准输入
    std::thread input_thread(input_loop, std::ref(peer));

    // 主线程负责打印接收到的消息
    while (peer.is_connected()) {
        Message msg;
        if (peer.try_recv_message(msg)) {
            // 成功从接收队列中弹出一个消息
            if (msg.content_type == MessageContentType::MSG_TEXT) {
                std::cout << "[" << msg.sender.name << "]: " << msg.content << std::endl;
            }
        }
        
        // 睡一小会儿，避免 CPU 100%
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 等待输入线程退出
    if (input_thread.joinable()) {
        input_thread.join();
    }
    std::cout << "[Main] Loop finished. Bye!" << std::endl;
}


int main(int argc, char* argv[]) {
    std::string mode;
    int port = 0;
    std::string ip;

    // --- 新的参数解析 ---
    if (argc < 3) {
        std::cerr << "Usage: \n"
                  << "  ./lab1_app listen <port> [MyName]\n"
                  << "  ./lab1_app connect <ip> <port> [MyName]\n";
        return 1;
    }

    mode = argv[1];

    if (mode == "listen") {
        if (argc < 3) {
             std::cerr << "Usage: ./lab1_app listen <port> [MyName]\n";
             return 1;
        }
        port = std::stoi(argv[2]);
        g_my_info.name = (argc > 3) ? argv[3] : "Server"; // <-- 修复 1

    } else if (mode == "connect") {
        if (argc < 4) {
            std::cerr << "Usage: ./lab1_app connect <ip> <port> [MyName]\n";
            return 1;
        }
        ip = argv[2];
        port = std::stoi(argv[3]);
        g_my_info.name = (argc > 4) ? argv[4] : "Client"; // <-- 修复 2

    } else {
        std::cerr << "Error: Unknown mode '" << mode << "'" << std::endl;
        return 1;
    }

    // --- 连接逻辑 (不变) ---
    TcpPeer peer;

    if (mode == "listen") {
        if (!peer.listen_on(port)) {
            return 1;
        }
    } else if (mode == "connect") {
        if (!peer.connect_to(ip, port)) {
            return 1;
        }
    }
    // --- 结束替换 ---

    // --- 运行聊天 (不变) ---
    std::cout << "--- Connection established! Your name is '" << g_my_info.name << "' ---" << std::endl;
    std::cout << "--- Type /quit to exit ---" << std::endl;
    
    run_chat_loop(peer);
    
    return 0;
}