#include <iostream>
#include <string>
#include <vector>
#include <map>          // 新增
#include <thread>
#include <mutex>
#include <fstream>
#include <iomanip>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <SDL2/SDL.h>

#include "tcp_peer.h"
#include "peer_message.h" 

// --- 全局状态 ---
std::shared_ptr<TcpPeer> g_peer = std::make_shared<TcpPeer>();

// [修改] 聊天记录分离： Key -> 聊天列表
// Key: 如果是群聊，使用特殊 ID；如果是私聊，使用对方用户名
std::map<std::string, std::vector<std::string>> g_chat_histories;
const std::string GROUP_ID = "___GROUP_CHAT___"; 
std::mutex g_chat_mutex; 

std::vector<std::string> g_online_users;
std::mutex g_online_users_mutex;

std::string g_private_chat_target; // 当前选中的私聊对象 (空表示群聊)

// [修改] 文件接收状态
struct FileRecvState {
    bool is_receiving = false;
    std::string filename;
    uint64_t total_size = 0;
    uint64_t received_size = 0;
    std::ofstream file_stream;
    std::string sender_name;
};
FileRecvState g_file_state;

// --- 辅助函数 ---

// [修改] 添加到指定历史
void AddToHistory(const std::string& target_key, std::string message) {
    std::lock_guard<std::mutex> lock(g_chat_mutex);
    g_chat_histories[target_key].push_back(std::move(message));
}

void UpdateUserList(std::vector<std::string> new_list) {
    std::lock_guard<std::mutex> lock(g_online_users_mutex);
    g_online_users = std::move(new_list);
}

enum class AppState { Connecting, Chatting };

// --- 主程序 ---
int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;
    
    // 创建窗口
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Lab3 Chat Client", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);

    // ImGui 初始化
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    // 请确保字体路径正确，否则中文可能乱码
    const char* font_path = "/mnt/c/Windows/Fonts/msyh.ttc"; 
    io.Fonts->AddFontFromFileTTF(font_path, 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    ImGui::StyleColorsDark();

    // 应用变量
    AppState app_state = AppState::Connecting;
    char ip_buffer[128] = "127.0.0.1"; 
    char port_buffer[32] = "9001";     
    char name_buffer[64] = "User";
    char message_buffer[1024] = "";
    char file_path_buffer[256] = "";

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // --- 1. 登录界面 ---
        if (app_state == AppState::Connecting) {
            ImGui::Begin("Login");
            ImGui::InputText("Username", name_buffer, 64);
            ImGui::InputText("IP", ip_buffer, 128);
            ImGui::InputText("Port", port_buffer, 32);

            if (ImGui::Button("Connect")) {
                std::string ip = ip_buffer;
                int port = std::stoi(port_buffer);
                std::string name = name_buffer;
                std::thread([ip, port, name]() { 
                    if (g_peer->connect_to(ip, port)) {
                        AddToHistory(GROUP_ID, "--- Connected ---");
                        SenderInfo sender; sender.name = name;
                        g_peer->send_message(Message::make_login_request(std::move(sender)));
                    }
                }).detach();
            }
            ImGui::End();

        // --- 2. 聊天界面 ---
        } else if (app_state == AppState::Chatting) {
            // A. 左侧列表
            ImGui::SetNextWindowSize(ImVec2(250, 500), ImGuiCond_FirstUseEver);
            ImGui::Begin("Online Users");
            
            if (ImGui::Button("Group Chat (All)")) {
                g_private_chat_target = ""; // 切换回群聊
            }
            ImGui::Separator();
            
            g_online_users_mutex.lock();
            for (const auto& user : g_online_users) {
                if (user == name_buffer) {
                    ImGui::Text("%s (You)", user.c_str());
                } else {
                    bool is_selected = (user == g_private_chat_target);
                    if (ImGui::Selectable(user.c_str(), is_selected)) {
                        g_private_chat_target = user; // 切换到私聊
                    }
                }
            }
            g_online_users_mutex.unlock();
            ImGui::End();

            // B. 聊天窗口
            std::string current_view_key = g_private_chat_target.empty() ? GROUP_ID : g_private_chat_target;
            std::string title = g_private_chat_target.empty() ? "Group Chat" : ("Private with " + g_private_chat_target);

            ImGui::Begin(title.c_str());
            ImGui::BeginChild("History", ImVec2(0, -120)); // 留空间给下方控件
            
            // 显示对应 Key 的历史记录
            g_chat_mutex.lock();
            const auto& history = g_chat_histories[current_view_key];
            for (const auto& line : history) {
                ImGui::TextWrapped("%s", line.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
            g_chat_mutex.unlock();
            
            ImGui::EndChild();
            ImGui::Separator();

            // C. 文本发送
            bool send_text = ImGui::InputText("Message", message_buffer, 1024, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Send") || send_text) {
                if (strlen(message_buffer) > 0) {
                    SenderInfo sender; sender.name = name_buffer;
                    Message msg;
                    // 发送逻辑 + 本地显示逻辑
                    if (g_private_chat_target.empty()) {
                        msg = Message::make_group_chat(std::move(sender), message_buffer);
                        AddToHistory(GROUP_ID, "[Me]: " + std::string(message_buffer));
                    } else {
                        msg = Message::make_private_chat(std::move(sender), g_private_chat_target, message_buffer);
                        AddToHistory(g_private_chat_target, "[Me -> " + g_private_chat_target + "]: " + std::string(message_buffer));
                    }
                    g_peer->send_message(std::move(msg));
                    memset(message_buffer, 0, 1024);
                    ImGui::SetKeyboardFocusHere(-1);
                }
            }

            // D. 文件发送 (支持群发和私发)
            ImGui::Separator();
            if (g_private_chat_target.empty()) ImGui::Text("Send File (Group Broadcast):");
            else ImGui::Text("Send File (Private):");

            ImGui::InputText("Path", file_path_buffer, 256);
            ImGui::SameLine();
            if (ImGui::Button("Send File")) {
                std::string filepath = file_path_buffer;
                std::string target = g_private_chat_target;
                std::string my_name = name_buffer;

                std::thread([filepath, target, my_name]() {
                    std::ifstream ifs(filepath, std::ios::binary | std::ios::ate);
                    if (!ifs) {
                        // 错误信息显示在当前视图
                        AddToHistory(target.empty() ? GROUP_ID : target, "[System]: Open file failed.");
                        return;
                    }
                    uint64_t filesize = ifs.tellg();
                    ifs.seekg(0, std::ios::beg);
                    std::string filename = filepath.substr(filepath.find_last_of("/\\") + 1);

                    AddToHistory(target.empty() ? GROUP_ID : target, "[System]: Sending '" + filename + "'...");

                    // 1. 发送 Header (使用 std::move 修复编译错误)
                    {
                        SenderInfo s; s.name = my_name;
                        g_peer->send_message(Message::make_file_header(std::move(s), target, filename, filesize));
                    }

                    // 2. 发送 Body
                    const size_t CHUNK = 8192;
                    char buf[CHUNK];
                    while (ifs.read(buf, CHUNK) || ifs.gcount() > 0) {
                        std::string data(buf, ifs.gcount());
                        SenderInfo s; s.name = my_name; // 每次循环新建
                        g_peer->send_message(Message::make_file_chunk(std::move(s), target, data));
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                    }
                    AddToHistory(target.empty() ? GROUP_ID : target, "[System]: File sent.");
                }).detach();
            }

            // E. 接收进度条
            if (g_file_state.is_receiving) {
                ImGui::Separator();
                float prog = (g_file_state.total_size > 0) ? (float)g_file_state.received_size / g_file_state.total_size : 0.0f;
                ImGui::Text("Receiving '%s' from %s...", g_file_state.filename.c_str(), g_file_state.sender_name.c_str());
                ImGui::ProgressBar(prog, ImVec2(0.0f, 0.0f));
            }

            ImGui::End();
        }

        // --- 状态检测 ---
        if (g_peer->is_connected() && app_state == AppState::Connecting) app_state = AppState::Chatting;
        if (!g_peer->is_connected() && app_state == AppState::Chatting) {
            app_state = AppState::Connecting;
            g_private_chat_target = "";
        }

        // --- 3. 消息接收处理 ---
        Message rmsg;
        while (g_peer->try_recv_message(rmsg)) {
            switch (rmsg.type) {
                case MessageType::MSG_CHAT:
                    if (rmsg.chat_mode == ChatMode::MODE_PRIVATE) {
                        // 私聊：存入对方名字的历史
                        if (rmsg.sender.name != name_buffer) {
                             AddToHistory(rmsg.sender.name, "[Private from " + rmsg.sender.name + "]: " + rmsg.content);
                        }
                    } else {
                        // 群聊：存入 GROUP_ID
                        // [关键] 过滤掉自己发的消息，防止回声
                        if (rmsg.sender.name != name_buffer) {
                            AddToHistory(GROUP_ID, "[" + rmsg.sender.name + "]: " + rmsg.content);
                        }
                    }
                    break;
                
                case MessageType::MSG_USER_JOIN_BCAST:
                    AddToHistory(GROUP_ID, "--- " + rmsg.sender.name + " joined ---");
                    UpdateUserList(std::move(rmsg.user_list));
                    break;
                case MessageType::MSG_USER_EXIT_BCAST:
                    AddToHistory(GROUP_ID, "--- " + rmsg.sender.name + " left ---");
                    UpdateUserList(std::move(rmsg.user_list));
                    break;
                case MessageType::MSG_USER_LIST_BCAST:
                    UpdateUserList(std::move(rmsg.user_list));
                    break;

                // --- 文件接收 ---
                case MessageType::MSG_FILE_HEADER:
                {
                    // [关键] 群发时，自己也会收到广播，必须忽略
                    if (rmsg.sender.name == name_buffer) break;

                    if (g_file_state.is_receiving) g_file_state.file_stream.close();
                    
                    g_file_state.is_receiving = true;
                    g_file_state.filename = rmsg.file_name;
                    g_file_state.total_size = rmsg.file_size;
                    g_file_state.received_size = 0;
                    g_file_state.sender_name = rmsg.sender.name;
                    
                    std::string save_name = "recv_" + rmsg.file_name;
                    g_file_state.file_stream.open(save_name, std::ios::binary);

                    std::string target_key = (rmsg.chat_mode == ChatMode::MODE_GROUP) ? GROUP_ID : rmsg.sender.name;
                    if (g_file_state.file_stream) {
                        AddToHistory(target_key, "[System]: Incoming file '" + rmsg.file_name + "'...");
                    } else {
                        AddToHistory(target_key, "[System]: Failed to create file.");
                        g_file_state.is_receiving = false;
                    }
                    break;
                }
                
                case MessageType::MSG_FILE_DATA:
                {
                    if (rmsg.sender.name == name_buffer) break; // 忽略自己的包

                    if (g_file_state.is_receiving && g_file_state.sender_name == rmsg.sender.name) {
                        g_file_state.file_stream.write(rmsg.content.data(), rmsg.content.size());
                        g_file_state.received_size += rmsg.content.size();

                        if (g_file_state.received_size >= g_file_state.total_size) {
                            g_file_state.file_stream.close();
                            g_file_state.is_receiving = false;
                            
                            std::string target_key = (rmsg.chat_mode == ChatMode::MODE_GROUP) ? GROUP_ID : rmsg.sender.name;
                            AddToHistory(target_key, "[System]: File saved as 'recv_" + g_file_state.filename + "'.");
                        }
                    }
                    break;
                }
                default: break;
            }
        }

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    g_peer->close_connection();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}