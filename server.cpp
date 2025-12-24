#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <winsock2.h>  // 必须在windows.h之前包含
#include <ws2tcpip.h>
#include <Windows.h>   // 调整到winsock2.h之后

#pragma comment(lib, "ws2_32.lib")

// 解决MinGW下inet_pton兼容问题
#ifndef inet_pton
const char *inet_pton(int af, const char *src, void *dst) {
    struct sockaddr_in sa;
    if (af == AF_INET) {
        if (inet_addr(src) == INADDR_NONE) return NULL;
        sa.sin_addr.s_addr = inet_addr(src);
        memcpy(dst, &sa.sin_addr, sizeof(struct in_addr));
        return src;
    }
    return NULL;
}
#endif

// 全局常量和变量
const std::string SERVER_IP = "192.168.5.55";
std::string ADMIN_PASSWORD;
int SERVER_PORT;
bool SERVER_RUNNING = true;

// 客户端信息结构体（修改为指针传递，避免拷贝thread）
struct ClientInfo {
    SOCKET socket;
    std::string username;
    bool is_admin;
    sockaddr_in addr;
    
    // 显式声明构造函数，避免默认拷贝
    ClientInfo() : socket(INVALID_SOCKET), is_admin(false) {}
    ClientInfo(const ClientInfo&) = delete;  // 禁用拷贝构造
    ClientInfo& operator=(const ClientInfo&) = delete; // 禁用赋值
    ClientInfo(ClientInfo&&) = default;      // 允许移动构造
    ClientInfo& operator=(ClientInfo&&) = default; // 允许移动赋值
};

std::vector<ClientInfo> clients;
std::mutex clients_mutex;

// 设置控制台文字颜色
void SetConsoleColor(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

// 广播消息给所有客户端
void BroadcastMessage(const std::string& message, SOCKET exclude_socket = INVALID_SOCKET) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto& client : clients) {
        if (client.socket != exclude_socket && client.socket != INVALID_SOCKET) {
            send(client.socket, message.c_str(), message.length(), 0);
        }
    }
}

// 处理单个客户端连接（改为接收指针，避免拷贝）
void HandleClient(ClientInfo* client_ptr) {
    if (!client_ptr) return;
    
    char buffer[1024];
    std::string welcome_msg;

    // 验证客户端身份
    recv(client_ptr->socket, buffer, sizeof(buffer), 0);
    std::string auth_data(buffer);
    
    // 解析认证数据: 类型|用户名/密码|端口
    size_t first_sep = auth_data.find('|');
    size_t second_sep = auth_data.find('|', first_sep + 1);
    std::string auth_type = auth_data.substr(0, first_sep);
    std::string auth_value = auth_data.substr(first_sep + 1, second_sep - first_sep - 1);
    
    // 管理员登录验证
    if (auth_type == "admin") {
        if (auth_value == ADMIN_PASSWORD) {
            client_ptr->is_admin = true;
            client_ptr->username = "admin";
            send(client_ptr->socket, "AUTH_SUCCESS|管理员权限", strlen("AUTH_SUCCESS|管理员权限"), 0);
            welcome_msg = "[系统] 管理员已登录\n";
            SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
            std::cout << welcome_msg;
            SetConsoleColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        } else {
            send(client_ptr->socket, "AUTH_FAILED|密码错误", strlen("AUTH_FAILED|密码错误"), 0);
            closesocket(client_ptr->socket);
            return;
        }
    }
    // 访客登录验证
    else if (auth_type == "guest") {
        // 检查用户名是否合法
        std::lock_guard<std::mutex> lock(clients_mutex);
        bool username_exists = false;
        for (const auto& c : clients) {
            if (c.username == auth_value || auth_value == "admin") {
                username_exists = true;
                break;
            }
        }
        
        if (username_exists) {
            send(client_ptr->socket, "AUTH_FAILED|用户名已存在或不合法", strlen("AUTH_FAILED|用户名已存在或不合法"), 0);
            closesocket(client_ptr->socket);
            return;
        }
        
        client_ptr->username = auth_value;
        send(client_ptr->socket, "AUTH_SUCCESS|访客权限", strlen("AUTH_SUCCESS|访客权限"), 0);
        welcome_msg = "[系统] 访客 " + client_ptr->username + " 已登录\n";
        std::cout << welcome_msg;
    }

    // 广播登录消息
    BroadcastMessage(welcome_msg, client_ptr->socket);

    // 消息循环
    while (SERVER_RUNNING) {
        ZeroMemory(buffer, sizeof(buffer));
        int bytes_received = recv(client_ptr->socket, buffer, sizeof(buffer), 0);
        
        if (bytes_received <= 0) {
            break;
        }

        std::string message(buffer);
        
        // 管理员指令处理
        if (client_ptr->is_admin) {
            if (message == "/shutdown") {
                BroadcastMessage("[系统] 管理员已关闭服务器\n");
                SERVER_RUNNING = false;
                break;
            } else if (message == "/restart") {
                BroadcastMessage("[系统] 服务器即将重启\n");
                SERVER_RUNNING = false;
                break;
            }
            
            // 管理员消息带红色前缀
            std::string admin_msg = "[管理员] " + client_ptr->username + ": " + message + "\n";
            SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
            std::cout << admin_msg;
            SetConsoleColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            BroadcastMessage(admin_msg, client_ptr->socket);
        }
        // 访客消息处理
        else {
            std::string guest_msg = "[访客] " + client_ptr->username + ": " + message + "\n";
            std::cout << guest_msg;
            BroadcastMessage(guest_msg, client_ptr->socket);
        }
    }

    // 客户端断开连接
    std::string disconnect_msg = "[系统] " + client_ptr->username + " 已退出\n";
    std::cout << disconnect_msg;
    BroadcastMessage(disconnect_msg, client_ptr->socket);

    // 清理客户端资源
    closesocket(client_ptr->socket);
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        if (it->socket == client_ptr->socket) {
            clients.erase(it);
            break;
        }
    }
    
    delete client_ptr; // 释放动态分配的客户端对象
}

int main() {
    // 初始化Windows Socket
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSA初始化失败！" << std::endl;
        return 1;
    }

    // 获取管理员密码和端口
    std::cout << "===== 内网通讯服务端 =====" << std::endl;
    std::cout << "请设置管理员密码: ";
    std::cin >> ADMIN_PASSWORD;
    std::cout << "请设置服务端端口: ";
    std::cin >> SERVER_PORT;

    // 创建服务器Socket
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "创建Socket失败！错误码: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // 绑定IP和端口
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP.c_str(), &server_addr.sin_addr);

    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "绑定失败！错误码: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    // 开始监听
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "监听失败！错误码: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "服务端已启动！IP: " << SERVER_IP << " 端口: " << SERVER_PORT << std::endl;
    std::cout << "等待客户端连接... (管理员密码: " << ADMIN_PASSWORD << ")" << std::endl;

    // 接受客户端连接
    while (SERVER_RUNNING) {
        sockaddr_in client_addr;
        int client_addr_size = sizeof(client_addr);
        SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_addr_size);

        if (client_socket == INVALID_SOCKET) {
            if (SERVER_RUNNING) {
                std::cerr << "接受连接失败！错误码: " << WSAGetLastError() << std::endl;
            }
            continue;
        }

        // 动态分配ClientInfo，避免栈拷贝thread
        ClientInfo* client_ptr = new ClientInfo();
        client_ptr->socket = client_socket;
        client_ptr->addr = client_addr;
        client_ptr->is_admin = false;

        // 将客户端加入列表
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients.push_back(std::move(*client_ptr)); // 移动语义，避免拷贝
        }

        // 创建线程处理客户端（传递指针，避免拷贝）
        std::thread handler_thread(HandleClient, client_ptr);
        handler_thread.detach();
    }

    // 清理资源
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto& client : clients) {
        closesocket(client.socket);
    }
    clients.clear();
    closesocket(server_socket);
    WSACleanup();

    std::cout << "服务端已关闭！" << std::endl;
    return 0;
}

