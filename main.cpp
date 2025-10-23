#include <iostream>
#include <string>
#include <vector>
#include <thread>

// ImGui 和后端
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

// SDL2
#include <SDL2/SDL.h>

// 我们的网络库
#include "tcp_peer.h"
#include "peer_message.h"

// 全局 Peer 对象
// 使用智能指针，以便在线程间安全地共享
std::shared_ptr<TcpPeer> g_peer = std::make_shared<TcpPeer>();

// 全局聊天记录
std::vector<std::string> g_chat_history;
std::mutex g_chat_mutex; // 保护 g_chat_history

// 添加一条消息到聊天记录 (线程安全)
void AddToHistory(std::string message) {
    std::lock_guard<std::mutex> lock(g_chat_mutex);
    g_chat_history.push_back(std::move(message));
}

// GUI 状态
enum class AppState {
    Connecting,
    Chatting,
    Disconnected
};


int main(int argc, char* argv[]) {
    // --- 1. 初始化 SDL ---
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "Error: " << SDL_GetError() << std::endl;
        return -1;
    }

    // --- 2. 创建窗口和渲染器 ---
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("P2P Chat (lab1)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);

    // --- 3. 初始化 ImGui ---
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    // (io.ConfigFlags |= ...) // 可选: 启用停靠和多视图

    // --- 新增：加载支持中文的字体 ---
    {
        // 1. 设置字体路径
        //    (这是从 WSL 访问 Windows 字体的标准路径)
        const char* font_path = "/mnt/c/Windows/Fonts/msyh.ttc"; // 微软雅黑
        float font_size = 18.0f; // 字体大小

        // 2. 定义要加载的字形范围
        //    我们必须合并 "默认" (ASCII) 和 "中文" 范围
        static ImVector<ImWchar> glyph_ranges;
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault()); // 添加默认 ASCII 范围
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon()); // 添加常用简体中文范围
        // (如果你需要日文, 可以添加: builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());)
        builder.BuildRanges(&glyph_ranges);

        // 3. 加载字体
        std::cout << "Loading font: " << font_path << std::endl;
        ImFont* font = io.Fonts->AddFontFromFileTTF(font_path, font_size, nullptr, glyph_ranges.Data);

        // 4. (重要) Fallback 机制
        //    如果加载字体失败 (比如文件不存在), 则加载回默认字体
        if (font == nullptr) {
            std::cerr << "Warning: Failed to load font '" << font_path << "'." << std::endl;
            std::cerr << "         Chinese/other languages will not display." << std::endl;
            std::cerr << "         Loading default font..." << std::endl;
            io.Fonts->AddFontDefault(); // 加载默认字体作为后备
        } else {
             std::cout << "Successfully loaded font." << std::endl;
        }
    }
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    ImGui::StyleColorsDark();

    // --- 4. 我们的状态变量 ---
    AppState app_state = AppState::Connecting;
    char ip_buffer[128] = "127.0.0.1";
    char port_buffer[32] = "9001";
    char name_buffer[64] = "User";
    char message_buffer[1024] = "";
    
    // --- 5. 主循环 ---
    bool running = true;
    while (running) {
        // --- 5a. 处理事件 ---
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        // --- 5b. ImGui 新一帧 ---
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // --- 5c. 绘制我们的 GUI ---
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);

        if (app_state == AppState::Connecting) {
            // === 绘制“连接”窗口 ===
            ImGui::Begin("Connect to Peer");
            ImGui::InputText("Your Name", name_buffer, 64);
            ImGui::InputText("IP Address", ip_buffer, 128);
            ImGui::InputText("Port", port_buffer, 32);

            if (ImGui::Button("Listen")) {
                std::cout << "Starting Listen thread..." << std::endl;
                int port = std::stoi(port_buffer);
                // (重要!) 在新线程中运行阻塞的 listen_on
                std::thread([port]() {
                    if (g_peer->listen_on(port)) {
                        AddToHistory("--- Successfully connected (as Server) ---");
                    } else {
                        AddToHistory("--- Listen failed ---");
                    }
                }).detach(); // detach() 让线程在后台运行
            }
            ImGui::SameLine();
            if (ImGui::Button("Connect")) {
                std::cout << "Starting Connect thread..." << std::endl;
                std::string ip = ip_buffer;
                int port = std::stoi(port_buffer);
                // (重要!) 在新线程中运行阻塞的 connect_to
                std::thread([ip, port]() {
                    if (g_peer->connect_to(ip, port)) {
                        AddToHistory("--- Successfully connected (as Client) ---");
                    } else {
                        AddToHistory("--- Connect failed ---");
                    }
                }).detach();
            }
            ImGui::End();

        } else if (app_state == AppState::Chatting) {
            // === 绘制“聊天”窗口 ===
            ImGui::Begin("Chat");
            
            // 显示聊天记录
            ImGui::BeginChild("History", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2));
            g_chat_mutex.lock();
            for (const auto& line : g_chat_history) {
                ImGui::TextWrapped("%s", line.c_str());
            }
            // (可选) 自动滚动到底部
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
            g_chat_mutex.unlock();
            ImGui::EndChild();

            // 输入框
            bool text_entered = ImGui::InputText("Message", message_buffer, 1024, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            bool send_clicked = ImGui::Button("Send");

            if (text_entered || send_clicked) {
                if (strlen(message_buffer) > 0) {
                    SenderInfo sender;
                    sender.name = name_buffer;
                    Message msg = Message::make_text(std::move(sender), std::string(message_buffer));
                    
                    // 1. 发送消息
                    g_peer->send_message(std::move(msg));

                    // 2. 立即在本地显示
                    std::string my_message = "[You]: ";
                    my_message += message_buffer;
                    AddToHistory(my_message);
                    
                    // 3. 清空输入框
                    memset(message_buffer, 0, 1024);
                    ImGui::SetKeyboardFocusHere(-1); // 重新聚焦输入框
                }
            }
            ImGui::End();
        }

        // --- 5d. 网络逻辑 (非阻塞) ---

        // 检查连接状态，切换窗口
        if (g_peer->is_connected() && app_state == AppState::Connecting) {
            app_state = AppState::Chatting;
        } else if (!g_peer->is_connected() && app_state == AppState::Chatting) {
            app_state = AppState::Connecting;
            AddToHistory("--- Connection lost ---");
        }

        // (重要!) 轮询接收消息
        Message received_msg;
        if (g_peer->try_recv_message(received_msg)) {
            if (received_msg.content_type == MessageContentType::MSG_TEXT) {
                std::string history_line = "[";
                history_line += received_msg.sender.name;
                history_line += "]: ";
                history_line += received_msg.content;
                AddToHistory(history_line);
            }
        }
        
        // --- 5e. 渲染 ---
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, (Uint8)(0.45f * 255), (Uint8)(0.55f * 255), (Uint8)(0.60f * 255), (Uint8)(1.00f * 255));
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);        SDL_RenderPresent(renderer);
    }

    // --- 6. 清理 ---
    g_peer->close_connection(); // 关闭网络连接
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}