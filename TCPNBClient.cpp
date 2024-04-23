#include <iostream>
#include <string>
#include <thread>
#include <Winsock2.h>

#pragma comment(lib, "WS2_32.lib")
#define BUF_SIZE 64  // 定义缓冲区大小

// 接收消息的函数，运行在单独的线程中
void receiveMessages(SOCKET sHost) {
    char buf[BUF_SIZE];  // 接收消息的缓冲区
    int retVal;  // recv函数的返回值

    while (true) {
        ZeroMemory(buf, BUF_SIZE);  // 清空缓冲区
        retVal = recv(sHost, buf, BUF_SIZE, 0);  // 接收数据
        if (retVal > 0) {  // 成功接收到数据
            buf[retVal] = '\0';  // 确保字符串以null终止
            std::cout << "Received from server: " << buf << std::endl;  // 打印接收到的消息
            if (strcmp(buf, "bye") == 0) {  // 检查是否接收到退出指令
                std::cout << "Server has closed the connection." << std::endl;
                break;
            }
        } else if (retVal == 0) {  // 连接被服务器端关闭
            std::cout << "Connection closed by the server." << std::endl;
            break;
        } else {  // 出错处理
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {  // 非阻塞错误检查
                std::cerr << "recv failed with error: " << err << std::endl;
                break;
            }
            Sleep(100);  // 非阻塞模式下，暂时无数据，稍后再试
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <Server IP> <Port>" << std::endl;
        return 1;
    }

    WSADATA wsd;
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {  // 初始化Winsock
        std::cerr << "WSAStartup failed !" << std::endl;
        return 1;
    }

    SOCKET sHost = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);  // 创建套接字
    if (sHost == INVALID_SOCKET) {
        std::cerr << "socket failed !" << std::endl;
        WSACleanup();
        return -1;
    }

    SOCKADDR_IN servAddr;  // 服务器地址结构
    servAddr.sin_family = AF_INET;  // 使用IPv4地址
    servAddr.sin_addr.s_addr = inet_addr(argv[1]);  // IP地址
    servAddr.sin_port = htons(static_cast<u_short>(std::atoi(argv[2])));  // 端口号

    if (connect(sHost, (LPSOCKADDR) &servAddr, sizeof(servAddr)) == SOCKET_ERROR) {  // 尝试连接服务器
        std::cerr << "Connect failed !" << std::endl;
        closesocket(sHost);
        WSACleanup();
        return -1;
    }

    std::cout << "Connected to server successfully." << std::endl;

    std::thread receiverThread(receiveMessages, sHost);  // 创建接收消息的线程
    std::string input;
    while (true) {
        std::getline(std::cin, input);  // 从标准输入读取数据
        if (send(sHost, input.c_str(), input.length(), 0) == SOCKET_ERROR) {  // 发送数据给服务器
            std::cerr << "send failed !" << std::endl;
            break;
        }
        if (input == "bye") {  // 如果输入的是"bye"，则退出
            break;
        }
    }

    receiverThread.join();  // 等待接收线程结束
    closesocket(sHost);  // 关闭套接字
    WSACleanup();  // 清理Winsock
    return 0;
}
