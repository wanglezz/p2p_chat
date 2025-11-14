#pragma once

#include "peer_message.h"
#include "thread_safe_queue.h"

#include <string>
#include <thread>
#include <atomic>
#include <memory>       // for std::shared_ptr
#include <functional>   // for std::function

class TcpPeerImpl; 

class TcpPeer {
public:
    TcpPeer();
    ~TcpPeer(); 


    /**
     * @brief 作为客户端连接到服务器
     */
    bool connect_to(const std::string& ip, int port);

    /**
     * @brief 作为服务器监听端口，等待客户端连接
     * @note 这是一个阻塞操作，直到有客户端连接进来
     */
    bool listen_on(int port);

    /**
     * @brief 关闭连接并停止所有线程
     */
    void close_connection();

    /**
     * @brief 将一条消息排队等待发送 (你已有的方法)
     */
    void send_message(Message&& msg);

    /**
     * @brief [非阻塞] 尝试从接收队列中获取一条消息
     * @param msg_out 如果成功，消息将被移动到这里
     * @return true 如果成功获取消息，false 如果队列为空
     */
    bool try_recv_message(Message& msg_out);

    /**
     * @brief 检查连接是否仍然活跃
     */
    bool is_connected() const;

private:
    ThreadSafeQueue<Message> send_queue_;
    std::atomic<bool> is_connected_ {false};
    std::atomic<bool> stop_flag_ {false};

    ThreadSafeQueue<Message> recv_queue_; // 用于接收消息
    std::thread sender_thread_;          // 发送线程
    std::thread receiver_thread_;        // 接收线程
    int socket_fd_ = -1;                 // socket 文件描述符

    void sender_loop();
    void receiver_loop();

    void start_threads(); // 启动两个循环
};