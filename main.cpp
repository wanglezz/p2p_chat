#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <fstream>
#include <iomanip>
#include <chrono> // 用于时间计算
#include <random> // 用于随机端口

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <SDL2/SDL.h>

#include "tcp_peer.h"
#include "peer_message.h" 

// --- 全局状态 ---
std::shared_ptr<TcpPeer> g_peer = std::make_shared<TcpPeer>(); // Server 连接

// 聊天记录分离: GroupID -> History / Username -> History
std::map<std::string, std::vector<std::string>> g_chat_histories;
const std::string GROUP_ID = "___GROUP_CHAT___"; 
std::mutex g_chat_mutex; 

std::vector<std::string> g_online_users;
std::mutex g_online_users_mutex;

std::string g_private_chat_target; // 空字符串代表群聊

// P2P 本机 IP 设置
char g_my_p2p_ip[128] = "127.0.0.1"; 

// --- 文件接收状态 (支持速率显示) ---
struct FileRecvState {
    bool is_receiving = false;
    std::string filename;
    uint64_t total_size = 0;
    uint64_t received_size = 0;
    std::ofstream file_stream;
    std::string sender_name;

    // 速率计算
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_tick;
    uint64_t bytes_last_tick = 0;
    double speed_mbps = 0.0;
    double eta_sec = 0.0;
};
FileRecvState g_file_state;

// --- Helper Functions ---
void AddToHistory(const std::string& target_key, std::string message) {
    std::lock_guard<std::mutex> lock(g_chat_mutex);
    g_chat_histories[target_key].push_back(std::move(message));
}

void UpdateUserList(std::vector<std::string> new_list) {
    std::lock_guard<std::mutex> lock(g_online_users_mutex);
    g_online_users = std::move(new_list);
}

// ==========================================
// P2P 专用逻辑
// ==========================================

// P2P 发送线程：监听端口 -> 发送数据
// [修复版]：filepath 按值传递，并在线程内部打开文件，防止 ifs 悬空引用崩溃
void P2P_Sender_Thread(std::string filepath, std::string target, std::string my_name, std::string my_ip) {
    // 1. 简单的预检查
    {
        std::ifstream check_ifs(filepath, std::ios::binary | std::ios::ate);
        if (!check_ifs) { 
            AddToHistory(target, "[P2P Error]: File open failed: " + filepath); 
            return; 
        }
    }

    // 2. 获取文件信息
    std::ifstream temp_ifs(filepath, std::ios::binary | std::ios::ate);
    uint64_t filesize = temp_ifs.tellg();
    std::string filename = filepath.substr(filepath.find_last_of("/\\") + 1);
    temp_ifs.close();

    // 3. 随机端口 (20000-30000)
    srand(time(0));
    int listen_port = 20000 + (rand() % 10000);
    
    // 4. 启动监听线程
    auto p2p_peer = std::make_shared<TcpPeer>();
    
    // [关键修复] 捕获列表去掉了 &ifs，改为捕获 filepath
    std::thread listener([p2p_peer, listen_port, filename, filesize, target, my_name, filepath]() {
        // 尝试监听
        if (!p2p_peer->listen_on(listen_port)) return; 

        // [关键修复] 在线程内部重新打开文件
        std::ifstream ifs(filepath, std::ios::binary);
        if (!ifs) return;

        // 连接建立，发送 Header
        { 
            SenderInfo s; s.name = my_name;
            p2p_peer->send_message(Message::make_file_header(std::move(s), target, filename, filesize)); 
        }
        
        // 发送 Body
        const size_t CHUNK = 64 * 1024; // P2P 可以用大包
        char buf[CHUNK];
        while (ifs.read(buf, CHUNK) || ifs.gcount() > 0) {
            std::string data(buf, ifs.gcount());
            SenderInfo s; s.name = my_name;
            p2p_peer->send_message(Message::make_file_chunk(std::move(s), target, data));
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 等待发送完毕
        p2p_peer->close_connection();
        AddToHistory(target, "[P2P System]: File sent successfully.");
    });
    listener.detach();

    // 5. 稍等监听就绪
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 6. 通过 Server 发送 P2P 信令
    AddToHistory(target, "[P2P System]: Signaling receiver (" + my_ip + ":" + std::to_string(listen_port) + ")...");
    SenderInfo s; s.name = my_name;
    g_peer->send_message(Message::make_p2p_request(std::move(s), target, filename, filesize, my_ip, listen_port));
}

// P2P 接收线程：连接 -> 接收 -> 算速
void P2P_Receiver_Thread(Message req_msg) {
    std::string sender = req_msg.sender.name;
    std::string ip = req_msg.p2p_server_ip;
    int port = req_msg.p2p_server_port;
    std::string filename = req_msg.file_name;
    uint64_t total = req_msg.file_size;

    AddToHistory(sender, "[P2P System]: Connecting " + ip + ":" + std::to_string(port) + "...");
    
    auto p2p_recv = std::make_shared<TcpPeer>();
    bool connected = false;
    for(int i=0; i<5; i++) { // 重试 5 次
        if(p2p_recv->connect_to(ip, port)) { connected = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    if (!connected) { AddToHistory(sender, "[P2P Error]: Connection failed."); return; }

    std::string save_name = "p2p_recv_" + filename;
    std::ofstream ofs(save_name, std::ios::binary);
    
    uint64_t recv_bytes = 0;
    auto start = std::chrono::steady_clock::now();
    auto last = start;
    uint64_t last_bytes = 0;

    Message msg;
    while (p2p_recv->is_connected()) {
        if (p2p_recv->try_recv_message(msg)) {
            if (msg.type == MessageType::MSG_FILE_DATA) {
                ofs.write(msg.content.data(), msg.content.size());
                recv_bytes += msg.content.size();

                // 速率计算 (控制台打印，避免刷屏 UI)
                auto now = std::chrono::steady_clock::now();
                std::chrono::duration<double> diff = now - last;
                if (diff.count() >= 1.0) {
                    double speed = (double)(recv_bytes - last_bytes) / (1024.0*1024.0) / diff.count();
                    std::cout << "\r[P2P Speed]: " << std::fixed << std::setprecision(2) << speed << " MB/s" << std::flush;
                    last = now; last_bytes = recv_bytes;
                }

                if (recv_bytes >= total) break;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    ofs.close();
    p2p_recv->close_connection();
    AddToHistory(sender, "[P2P System]: Download finished: " + save_name);
}

// ==========================================
// 主逻辑
// ==========================================
int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;
    SDL_Window* window = SDL_CreateWindow("Lab3 Chat (P2P Enhanced)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI));
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); 
    // 中文字体设置 (确保路径有效)
    const char* font_path = "/mnt/c/Windows/Fonts/msyh.ttc"; 
    // 尝试加载字体，如果失败则使用默认
    if (std::ifstream(font_path).good()) {
        io.Fonts->AddFontFromFileTTF(font_path, 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    } else {
        std::cerr << "Warning: Font not found, using default." << std::endl;
        io.Fonts->AddFontDefault();
    }
    
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    ImGui::StyleColorsDark();

    enum class AppState { Connecting, Chatting };
    AppState app_state = AppState::Connecting;
    char ip_buf[128]="127.0.0.1", port_buf[32]="9001", name_buf[64]="User", msg_buf[1024]="", file_buf[256]="";
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

        if (app_state == AppState::Connecting) {
            ImGui::Begin("Login");
            ImGui::InputText("User", name_buf, 64);
            ImGui::InputText("Server IP", ip_buf, 128);
            ImGui::InputText("Port", port_buf, 32);
            if (ImGui::Button("Connect")) {
                std::string ip = ip_buf; int p = std::stoi(port_buf); std::string n = name_buf;
                std::thread([ip, p, n](){
                    if (g_peer->connect_to(ip, p)) {
                        AddToHistory(GROUP_ID, "--- Connected ---");
                        SenderInfo s; s.name = n;
                        g_peer->send_message(Message::make_login_request(std::move(s)));
                    }
                }).detach();
            }
            ImGui::End();
        } 
        else if (app_state == AppState::Chatting) {
            // 左侧栏
            ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_FirstUseEver);
            ImGui::Begin("Users & P2P Settings");
            
            if (ImGui::Button("Group Chat")) g_private_chat_target = "";
            ImGui::Separator();
            g_online_users_mutex.lock();
            for (const auto& u : g_online_users) {
                if (u == name_buf) ImGui::Text("%s (You)", u.c_str());
                else if (ImGui::Selectable(u.c_str(), u == g_private_chat_target)) g_private_chat_target = u;
            }
            g_online_users_mutex.unlock();
            
            ImGui::Separator();
            ImGui::Text("My P2P IP (For direct connect)");
            ImGui::InputText("##P2PIP", g_my_p2p_ip, 128);
            ImGui::End();

            // 聊天窗口
            std::string view_key = g_private_chat_target.empty() ? GROUP_ID : g_private_chat_target;
            std::string title = g_private_chat_target.empty() ? "Group Chat" : ("Private: " + g_private_chat_target);
            ImGui::Begin(title.c_str());
            ImGui::BeginChild("History", ImVec2(0, -150));
            g_chat_mutex.lock();
            for (const auto& line : g_chat_histories[view_key]) ImGui::TextWrapped("%s", line.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
            g_chat_mutex.unlock();
            ImGui::EndChild();

            // 发送区
            bool enter = ImGui::InputText("Msg", msg_buf, 1024, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Send") || enter) {
                if (strlen(msg_buf) > 0) {
                    SenderInfo s; s.name = name_buf;
                    Message m;
                    if (g_private_chat_target.empty()) {
                        m = Message::make_group_chat(std::move(s), msg_buf);
                        AddToHistory(GROUP_ID, "[Me]: " + std::string(msg_buf));
                    } else {
                        m = Message::make_private_chat(std::move(s), g_private_chat_target, msg_buf);
                        AddToHistory(g_private_chat_target, "[Me -> " + g_private_chat_target + "]: " + std::string(msg_buf));
                    }
                    g_peer->send_message(std::move(m));
                    memset(msg_buf, 0, 1024);
                    ImGui::SetKeyboardFocusHere(-1);
                }
            }

            ImGui::Separator();
            ImGui::InputText("File Path", file_buf, 256);
            
            // Server Relay 发送
            if (ImGui::Button("Server Send")) {
                std::string path = file_buf, target = g_private_chat_target, my = name_buf;
                std::thread([path, target, my](){
                    std::ifstream ifs(path, std::ios::binary|std::ios::ate);
                    if(!ifs){ AddToHistory(target.empty()?GROUP_ID:target, "File Error"); return; }
                    uint64_t size = ifs.tellg(); ifs.seekg(0, std::ios::beg);
                    std::string fname = path.substr(path.find_last_of("/\\")+1);
                    
                    { SenderInfo s; s.name = my;
                      g_peer->send_message(Message::make_file_header(std::move(s), target, fname, size)); }
                    
                    char buf[8192];
                    while(ifs.read(buf, 8192) || ifs.gcount()>0) {
                        std::string data(buf, ifs.gcount());
                        SenderInfo s; s.name = my;
                        g_peer->send_message(Message::make_file_chunk(std::move(s), target, data));
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                    }
                    AddToHistory(target.empty()?GROUP_ID:target, "[System] Server Relay Sent.");
                }).detach();
            }

            // P2P Direct 发送 (仅限私聊)
            if (!g_private_chat_target.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("P2P Send")) {
                    std::string path = file_buf, target = g_private_chat_target, my = name_buf, ip = g_my_p2p_ip;
                    std::thread(P2P_Sender_Thread, path, target, my, ip).detach();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Direct transfer. No Server bottleneck.");
            }

            // 接收进度与速率
            if (g_file_state.is_receiving) {
                ImGui::Separator();
                float p = g_file_state.total_size > 0 ? (float)g_file_state.received_size/g_file_state.total_size : 0;
                ImGui::Text("Recv '%s' (%.1f%%)", g_file_state.filename.c_str(), p*100);
                ImGui::ProgressBar(p, ImVec2(-1, 0));
                if (g_file_state.speed_mbps > 0) 
                    ImGui::Text("Speed: %.2f MB/s | ETA: %.1f s", g_file_state.speed_mbps, g_file_state.eta_sec);
            }
            ImGui::End();
        }

        if (g_peer->is_connected() && app_state == AppState::Connecting) app_state = AppState::Chatting;
        if (!g_peer->is_connected() && app_state == AppState::Chatting) {
            app_state = AppState::Connecting; g_private_chat_target = "";
        }

        // --- 接收循环 ---
        Message rmsg;
        while (g_peer->try_recv_message(rmsg)) {
            switch (rmsg.type) {
                case MessageType::MSG_CHAT:
                    if (rmsg.chat_mode == ChatMode::MODE_GROUP) {
                        if (rmsg.sender.name != name_buf) // 防回声
                            AddToHistory(GROUP_ID, "[" + rmsg.sender.name + "]: " + rmsg.content);
                    } else {
                        if (rmsg.sender.name != name_buf)
                            AddToHistory(rmsg.sender.name, "[Private]: " + rmsg.content);
                    }
                    break;
                case MessageType::MSG_USER_JOIN_BCAST:
                case MessageType::MSG_USER_EXIT_BCAST:
                    AddToHistory(GROUP_ID, "--- " + rmsg.sender.name + (rmsg.type == MessageType::MSG_USER_JOIN_BCAST ? " joined" : " left"));
                    UpdateUserList(std::move(rmsg.user_list));
                    break;
                case MessageType::MSG_USER_LIST_BCAST:
                    UpdateUserList(std::move(rmsg.user_list));
                    break;

                // Relay File Recv
                case MessageType::MSG_FILE_HEADER: {
                    if (rmsg.sender.name == name_buf) break; 
                    if (g_file_state.is_receiving) g_file_state.file_stream.close();
                    g_file_state.is_receiving = true;
                    g_file_state.filename = rmsg.file_name;
                    g_file_state.total_size = rmsg.file_size;
                    g_file_state.received_size = 0;
                    g_file_state.sender_name = rmsg.sender.name;
                    g_file_state.start_time = std::chrono::steady_clock::now();
                    g_file_state.last_tick = g_file_state.start_time;
                    g_file_state.bytes_last_tick = 0;
                    g_file_state.file_stream.open("recv_" + rmsg.file_name, std::ios::binary);
                    
                    std::string key = (rmsg.chat_mode == ChatMode::MODE_GROUP) ? GROUP_ID : rmsg.sender.name;
                    AddToHistory(key, "[System] Incoming file...");
                    break;
                }
                case MessageType::MSG_FILE_DATA: {
                    if (rmsg.sender.name == name_buf) break;
                    if (g_file_state.is_receiving && g_file_state.sender_name == rmsg.sender.name) {
                        g_file_state.file_stream.write(rmsg.content.data(), rmsg.content.size());
                        g_file_state.received_size += rmsg.content.size();

                        // 算速
                        auto now = std::chrono::steady_clock::now();
                        std::chrono::duration<double> diff = now - g_file_state.last_tick;
                        if (diff.count() >= 0.5) {
                            uint64_t d_bytes = g_file_state.received_size - g_file_state.bytes_last_tick;
                            g_file_state.speed_mbps = (double)d_bytes / 1048576.0 / diff.count();
                            uint64_t rem = g_file_state.total_size - g_file_state.received_size;
                            g_file_state.eta_sec = (d_bytes > 0) ? (double)rem / ((double)d_bytes/diff.count()) : 999;
                            g_file_state.last_tick = now; g_file_state.bytes_last_tick = g_file_state.received_size;
                        }

                        if (g_file_state.received_size >= g_file_state.total_size) {
                            g_file_state.file_stream.close();
                            g_file_state.is_receiving = false;
                            std::string key = (rmsg.chat_mode == ChatMode::MODE_GROUP) ? GROUP_ID : rmsg.sender.name;
                            AddToHistory(key, "[System] File saved.");
                        }
                    }
                    break;
                }

                // P2P Signal Recv
                case MessageType::MSG_P2P_TRANS_REQ: {
                    std::thread(P2P_Receiver_Thread, std::move(rmsg)).detach();
                    break;
                }
                default: break;
            }
        }

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }
    
    g_peer->close_connection();
    ImGui_ImplSDLRenderer2_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}