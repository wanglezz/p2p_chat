#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>      // *** 1. 新增: 包含 <mutex> ***

// ImGui 和后端
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

// SDL2
#include <SDL2/SDL.h>

// 我们的网络库
#include "tcp_peer.h"
#include "peer_message.h" // 确保这是 Lab 2 的新版本

// 全局 Peer 对象
std::shared_ptr<TcpPeer> g_peer = std::make_shared<TcpPeer>();

// --- 2. 新/旧 全局变量 ---
std::vector<std::string> g_chat_history;
std::mutex g_chat_mutex; 

std::vector<std::string> g_online_users;
std::mutex g_online_users_mutex;
std::string g_private_chat_target; // 用于跟踪私聊对象
// ---

// 添加一条消息到聊天记录 (线程安全)
void AddToHistory(std::string message) {
    std::lock_guard<std::mutex> lock(g_chat_mutex);
    g_chat_history.push_back(std::move(message));
}

// 更新在线用户列表 (线程安全)
void UpdateUserList(std::vector<std::string> new_list) {
    std::lock_guard<std::mutex> lock(g_online_users_mutex);
    g_online_users = std::move(new_list);
}

// GUI 状态
enum class AppState {
    Connecting, // 也可以叫 Login
    Chatting,
    Disconnected // (这个状态在当前逻辑中未使用，但保留)
};


int main(int argc, char* argv[]) {
    // --- 1. 初始化 SDL --- (不变)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "Error: " << SDL_GetError() << std::endl;
        return -1;
    }

    // --- 2. 创建窗口和渲染器 --- (不变)
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Multi-Chat Client (Lab 2)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);

    // --- 3. 初始化 ImGui (包括字体) --- (不变)
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    {
        const char* font_path = "/mnt/c/Windows/Fonts/msyh.ttc";
        float font_size = 18.0f;
        static ImVector<ImWchar> glyph_ranges;
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        builder.BuildRanges(&glyph_ranges);
        std::cout << "Loading font: " << font_path << std::endl;
        ImFont* font = io.Fonts->AddFontFromFileTTF(font_path, font_size, nullptr, glyph_ranges.Data);
        if (font == nullptr) {
            std::cerr << "Warning: Failed to load font. Using default." << std::endl;
            io.Fonts->AddFontDefault();
        } else {
             std::cout << "Successfully loaded font." << std::endl;
        }
    }
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    ImGui::StyleColorsDark();

    // --- 4. 我们的状态变量 --- (不变)
    AppState app_state = AppState::Connecting;
    char ip_buffer[128] = "127.0.0.1"; // 默认服务器 IP
    char port_buffer[32] = "9001";     // 默认服务器端口
    char name_buffer[64] = "User";
    char message_buffer[1024] = "";
    
    // --- 5. 主循环 ---
    bool running = true;
    while (running) {
        // --- 5a. 处理事件 --- (不变)
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        // --- 5b. ImGui 新一帧 --- (不变)
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // --- 5c. 绘制我们的 GUI ---
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);

        // --- 3. "连接" 界面 (已修改) ---
        if (app_state == AppState::Connecting) {
            ImGui::Begin("Login to Server");
            ImGui::InputText("Your Name", name_buffer, 64);
            ImGui::InputText("Server IP", ip_buffer, 128);
            ImGui::InputText("Server Port", port_buffer, 32);

            // *** "Listen" 按钮已删除 ***

            if (ImGui::Button("Login")) {
                std::cout << "Starting Connect thread..." << std::endl;
                std::string ip = ip_buffer;
                int port = std::stoi(port_buffer);
                std::string name = name_buffer; // 捕获用户名

                // (重要!) 在新线程中运行阻塞的 connect_to
                std::thread([ip, port, name]() { // *** 传递 name ***
                    if (g_peer->connect_to(ip, port)) {
                        AddToHistory("--- Successfully connected to server ---");
                        
                        // *** 立即发送登录请求 ***
                        SenderInfo sender;
                        sender.name = name;
                        Message login_msg = Message::make_login_request(std::move(sender));
                        g_peer->send_message(std::move(login_msg));
                        
                    } else {
                        AddToHistory("--- Connect failed ---");
                    }
                }).detach();
            }
            ImGui::End();

        // --- 4. "聊天" 界面 (已修改) ---
        } else if (app_state == AppState::Chatting) {
            
            // === 绘制“在线用户”窗口 (新增) ===
            ImGui::SetNextWindowSize(ImVec2(200, 400), ImGuiCond_FirstUseEver);
            ImGui::Begin("Online Users");
            
            // 按钮：点击以清除私聊目标，返回群聊
            if (ImGui::Button("Chat with: [GROUP]")) {
                g_private_chat_target = "";
            }
            ImGui::Separator();
            
            g_online_users_mutex.lock();
            for (const auto& user : g_online_users) {
                if (user == name_buffer) {
                    ImGui::Text("%s (You)", user.c_str());
                } else {
                    // 使命字可被点击，用于选择私聊
                    if (ImGui::Selectable(user.c_str(), user == g_private_chat_target)) {
                        g_private_chat_target = user;
                    }
                }
            }
            g_online_users_mutex.unlock();
            ImGui::End();


            // === 绘制“聊天”窗口 (修改) ===
            std::string chat_title = "Chat - ";
            if (g_private_chat_target.empty()) {
                chat_title += "[GROUP]";
            } else {
                chat_title += "[Private with " + g_private_chat_target + "]";
            }
            ImGui::Begin(chat_title.c_str());
            
            // 显示聊天记录 (不变)
            ImGui::BeginChild("History", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2));
            g_chat_mutex.lock();
            for (const auto& line : g_chat_history) {
                ImGui::TextWrapped("%s", line.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
            g_chat_mutex.unlock();
            ImGui::EndChild();

            // 输入框 (修改了发送逻辑)
            bool text_entered = ImGui::InputText("Message", message_buffer, 1024, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            bool send_clicked = ImGui::Button("Send");

            if (text_entered || send_clicked) {
                if (strlen(message_buffer) > 0) {
                    SenderInfo sender;
                    sender.name = name_buffer;
                    std::string content = message_buffer;
                    
                    Message msg;
                    std::string my_message_prefix;

                    // *** 检查是群聊还是私聊 ***
                    if (g_private_chat_target.empty()) {
                        // 1. 创建群聊消息
                        msg = Message::make_group_chat(std::move(sender), content);
                        my_message_prefix = "[You to Group]: ";
                    } else {
                        // 2. 创建私聊消息
                        msg = Message::make_private_chat(std::move(sender), g_private_chat_target, content);
                        my_message_prefix = "[You to " + g_private_chat_target + "]: ";
                    }
                    
                    g_peer->send_message(std::move(msg));

                    // 立即在本地显示
                    AddToHistory(my_message_prefix + content);
                    
                    // 清空输入框
                    memset(message_buffer, 0, 1024);
                    ImGui::SetKeyboardFocusHere(-1); // 重新聚焦输入框
                }
            }
            ImGui::End();
        }

        // --- 5d. 网络逻辑 (非阻塞) ---

        // 检查连接状态，切换窗口 (不变)
        if (g_peer->is_connected() && app_state == AppState::Connecting) {
            app_state = AppState::Chatting;
        } else if (!g_peer->is_connected() && app_state == AppState::Chatting) {
            app_state = AppState::Connecting;
            AddToHistory("--- Connection lost ---");
            UpdateUserList({}); // 清空用户列表
            g_private_chat_target = ""; // 重置私聊
        }

        // --- 5. "接收" 逻辑 (已修改) ---
        Message received_msg;
        if (g_peer->try_recv_message(received_msg)) {
            std::string history_line;
            
            // *** 使用 switch 处理所有新消息类型 ***
            switch (received_msg.type) {
                case MessageType::MSG_CHAT:
                    if (received_msg.chat_mode == ChatMode::MODE_PRIVATE) {
                        history_line = "[Private from " + received_msg.sender.name + "]: ";
                    } else {
                        history_line = "[" + received_msg.sender.name + "]: ";
                    }
                    history_line += received_msg.content;
                    AddToHistory(history_line);
                    break;
                
                case MessageType::MSG_USER_JOIN_BCAST:
                    history_line = "--- User '" + received_msg.sender.name + "' has joined. ---";
                    AddToHistory(history_line);
                    UpdateUserList(std::move(received_msg.user_list));
                    break;
                    
                case MessageType::MSG_USER_EXIT_BCAST:
                    history_line = "--- User '" + received_msg.sender.name + "' has left. ---";
                    AddToHistory(history_line);
                    UpdateUserList(std::move(received_msg.user_list));
                    break;

                case MessageType::MSG_USER_LIST_BCAST:
                    // (通常在刚登录时收到)
                    AddToHistory("--- You are now online. ---");
                    UpdateUserList(std::move(received_msg.user_list));
                    break;

                case MessageType::MSG_SYS_ANNOUNCE_BCAST:
                    history_line = "[SYSTEM ANNOUNCEMENT]: " + received_msg.content;
                    AddToHistory(history_line);
                    break;
                
                default:
                    // 忽略 MSG_LOGIN_REQUEST 或其他未知类型
                    break;
            }
        }
        
        // --- 5e. 渲染 --- (不变)
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, (Uint8)(0.45f * 255), (Uint8)(0.55f * 255), (Uint8)(0.60f * 255), (Uint8)(1.00f * 255));
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    // --- 6. 清理 --- (不变)
    g_peer->close_connection(); // 关闭网络连接
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}