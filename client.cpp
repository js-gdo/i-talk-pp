#include <iostream>
#include <string>
#include <thread>
#include <winsock2.h>
#include <Windows.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

const std::string SERVER_IP = "192.168.5.55";
bool CLIENT_RUNNING = true;
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
// 设置控制台文字颜色
void SetConsoleColor(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

// 接收服务器消息的线程函数
void ReceiveMessages(SOCKET client_socket) {
    char buffer[1024];
    while (CLIENT_RUNNING) {
        ZeroMemory(buffer, sizeof(buffer));
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        
        if (bytes_received <= 0) {
            std::cout << "\n与服务器断开连接！" << std::endl;
            CLIENT_RUNNING = false;
            break;
        }

        std::string message(buffer);
        // 管理员消息显示为红色
        if (message.find("[管理员]") != std::string::npos) {
            SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
            std::cout << message;
            SetConsoleColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        } else {
            std::cout << message;
        }
    }
}

int main() {
    // 初始化Windows Socket
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSA初始化失败！" << std::endl;
        return 1;
    }

    // 选择登录类型
    int login_type;
    std::cout << "===== 内网通讯客户端 =====" << std::endl;
    std::cout << "请选择登录类型:" << std::endl;
    std::cout << "1. 管理员登录" << std::endl;
    std::cout << "2. 访客登录" << std::endl;
    std::cout << "输入选项(1/2): ";
    std::cin >> login_type;
    std::cin.ignore();

    // 获取端口
    int server_port;
    std::cout << "请输入服务器端口: ";
    std::cin >> server_port;
    std::cin.ignore();

    // 创建客户端Socket
    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "创建Socket失败！错误码: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // 连接服务器
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, SERVER_IP.c_str(), &server_addr.sin_addr);

    std::cout << "正在连接服务器 " << SERVER_IP << ":" << server_port << "..." << std::endl;
    if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "连接失败！错误码: " << WSAGetLastError() << std::endl;
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "已连接到服务器！" << std::endl;

    // 认证流程
    std::string auth_data;
    if (login_type == 1) {
        // 管理员登录
        std::string password;
        std::cout << "请输入管理员密码: ";
        std::cin >> password;
        std::cin.ignore();
        auth_data = "admin|" + password + "|" + std::to_string(server_port);
    } else if (login_type == 2) {
        // 访客登录
        std::string username;
        std::cout << "请输入用户名(不能是admin): ";
        std::cin >> username;
        std::cin.ignore();
        auth_data = "guest|" + username + "|" + std::to_string(server_port);
    } else {
        std::cerr << "无效的登录类型！" << std::endl;
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    // 发送认证数据
    send(client_socket, auth_data.c_str(), auth_data.length(), 0);

    // 接收认证结果
    char buffer[1024];
    recv(client_socket, buffer, sizeof(buffer), 0);
    std::string auth_result(buffer);
    
    if (auth_result.find("AUTH_FAILED") != std::string::npos) {
        std::cerr << "登录失败: " << auth_result.substr(12) << std::endl;
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "登录成功！" << auth_result.substr(12) << std::endl;
    std::cout << "输入消息即可发送，管理员可输入 /shutdown 关闭服务器" << std::endl;
    std::cout << "==========================================" << std::endl;

    // 启动接收消息线程
    std::thread recv_thread(ReceiveMessages, client_socket);
    recv_thread.detach();

    // 发送消息循环
    std::string message;
    while (CLIENT_RUNNING) {
        std::getline(std::cin, message);
        if (message.empty()) continue;
        
        send(client_socket, message.c_str(), message.length(), 0);
        
        // 管理员关闭服务器指令
        if (login_type == 1 && message == "/shutdown") {
            CLIENT_RUNNING = false;
            break;
        }
    }

    // 清理资源
    closesocket(client_socket);
    WSACleanup();
    std::cout << "客户端已退出！" << std::endl;

    return 0;
}

