/**
 * Copyright (c) 2025. All rights reserved.
 * @file file_manager.h
 * @brief 文件传输管理器
 * @details 负责文件元数据获取、分块读取以及接收端的重组写入。
 * 遵循 Huawei 编程规范：大驼峰函数名、清晰注释、RAII。
 */

#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <map>
#include <mutex>
#include <cstdint>

/**
 * @brief 文件传输管理类 (静态工具类)
 */
class FileManager {
public:
    /**
     * @brief 接收文件的会话状态
     */
    struct ReceiveSession {
        std::ofstream fileStream;   ///< 文件写入流
        std::string fileName;       ///< 原始文件名
        std::string localFilePath;  ///< 本地保存路径
        uint64_t totalSize = 0;     ///< 预计总大小
        uint64_t receivedSize = 0;  ///< 已接收大小
        bool isValid = false;       ///< 会话是否有效
    };

    // --- 发送端方法 ---

    /**
     * @brief 获取文件元数据（文件名、大小）
     * @param[in] filePath 文件完整路径
     * @param[out] outName 提取出的文件名
     * @param[out] outSize 文件大小 (字节)
     * @return true 成功, false 文件不存在或不可读
     */
    static bool GetFileMetadata(const std::string& filePath, std::string& outName, uint64_t& outSize);

    /**
     * @brief 读取文件的一个分块
     * @param[in] filePath 文件路径
     * @param[in] offset 读取起始偏移量
     * @param[in] chunkSize 本次期望读取的大小
     * @param[out] outData 读取到的二进制数据
     * @return size_t 实际读取的字节数 (0 表示结束或错误)
     */
    static size_t ReadFileChunk(const std::string& filePath, uint64_t offset, size_t chunkSize, std::vector<uint8_t>& outData);

    // --- 接收端方法 ---

    /**
     * @brief 初始化接收会话（收到 FileHeader 时调用）
     * @param[in] fileId 文件的唯一标识符
     * @param[in] fileName 文件名
     * @param[in] fileSize 文件总大小
     * @return true 初始化成功, false 失败（如无法创建文件）
     */
    static bool InitReceiveSession(const std::string& fileId, const std::string& fileName, uint64_t fileSize);

    /**
     * @brief 写入接收到的分块（收到 FileChunk 时调用）
     * @param[in] fileId 文件唯一标识符
     * @param[in] data 二进制分块数据
     * @return true 写入成功, false 会话不存在或写入失败
     */
    static bool WriteFileChunk(const std::string& fileId, const std::vector<uint8_t>& data);

    /**
     * @brief 检查文件是否接收完毕
     * @param[in] fileId 文件唯一标识符
     * @return true 已接收完所有字节
     */
    static bool IsFileTransferComplete(const std::string& fileId);

    /**
     * @brief 关闭接收会话并清理资源
     * @param[in] fileId 文件唯一标识符
     */
    static void CloseReceiveSession(const std::string& fileId);

    /**
     * @brief 获取接收进度（百分比 0.0 - 100.0）
     */
    static float GetReceiveProgress(const std::string& fileId);

private:
    // 存储正在接收的文件会话：FileID -> Session
    // 使用 map 管理多个并发的文件传输
    static std::map<std::string, ReceiveSession> s_receiveSessions;
    
    // 保护 s_receiveSessions 的互斥锁，确保多线程安全
    static std::mutex s_sessionMutex;
};