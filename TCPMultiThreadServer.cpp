#include <iostream>
#include <winsock2.h>
#include <windows.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>

#define BUF_SIZE 64

struct ClientInfo {
    int id;
    SOCKET sclient;
    sockaddr_in addrClient;
    OVERLAPPED overlapped;
    char buf[BUF_SIZE];
};

std::mutex connectionMutex;
std::atomic<int> connectionCount(0);
std::atomic<int> nextClientId(1);
std::vector<ClientInfo *> clients;

// 函数声明
void ProcessClient(ClientInfo *clientInfo);
DWORD WINAPI KeyboardThread(LPVOID);
void Cleanup();

int main() {
    WSADATA wsaData;
    SOCKET sServer;
    int retVal;

    // 初始化Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed !" << std::endl;
        return 1;
    }

    // 创建服务器套接字
    sServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sServer == INVALID_SOCKET) {
        std::cerr << "Socket failed !" << std::endl;
        WSACleanup();
        return -1;
    }

    // 设置服务器地址信息并绑定
    sockaddr_in addrServ{};
    addrServ.sin_family = AF_INET;
    int port = 9990;
    addrServ.sin_port = htons(port);
    addrServ.sin_addr.S_un.S_addr = INADDR_ANY;

    retVal = bind(sServer, (sockaddr *) &addrServ, sizeof(addrServ));
    if (retVal == SOCKET_ERROR) {
        std::cerr << "Bind failed !" << std::endl;
        closesocket(sServer);
        WSACleanup();
        return -1;
    }

    // 开始监听
    retVal = listen(sServer, SOMAXCONN);
    if (retVal == SOCKET_ERROR) {
        std::cerr << "Listen failed !" << std::endl;
        closesocket(sServer);
        WSACleanup();
        return -1;
    }

    // 创建键盘输入线程
    CreateThread(nullptr, 0, KeyboardThread, nullptr, 0, nullptr);

    std::cout << "Server is listening on port " << port << " ..." << std::endl;

    // 循环等待客户端连接
    while (true) {
        sockaddr_in addrClient{};
        int addrClientLen = sizeof(addrClient);
        SOCKET sClient = accept(sServer, (sockaddr *) &addrClient, &addrClientLen);
        if (sClient == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                std::cerr << "Accept failed with error: " << err << std::endl;
                break;
            }
            Sleep(100);
            continue;
        }

        // 为新客户端分配内存
        auto *clientInfo = new ClientInfo;
        clientInfo->sclient = sClient;
        clientInfo->addrClient = addrClient;
        clientInfo->id = nextClientId++;
        ZeroMemory(&clientInfo->overlapped, sizeof(clientInfo->overlapped));

        // 创建线程处理客户端请求
        std::thread(ProcessClient, clientInfo).detach();
    }

    // 服务结束后的清理工作
    Cleanup();
    return 0;
}

// 客户端请求处理函数
void ProcessClient(ClientInfo *clientInfo) {
    // 创建事件对象并关联到OVERLAPPED结构体
    clientInfo->overlapped.hEvent = WSACreateEvent();
    if (clientInfo->overlapped.hEvent == NULL) {
        std::cerr << "WSACreateEvent failed with error: " << WSAGetLastError() << std::endl;
        return;
    }

    // 客户端连接信息
    connectionMutex.lock();
    connectionCount++;
    std::cout << "Client [" << clientInfo->id << "] connected from "
              << inet_ntoa(clientInfo->addrClient.sin_addr) << ":" << ntohs(clientInfo->addrClient.sin_port)
              << std::endl;
    clients.push_back(clientInfo);
    std::cout << "Total connections: " << connectionCount.load() << std::endl;
    connectionMutex.unlock();

    // 进入通信循环
    while (true) {
        DWORD bytesReceived;
        DWORD flags = 0;
        WSABUF dataBuf;
        dataBuf.buf = clientInfo->buf;
        dataBuf.len = BUF_SIZE;

        int retVal = WSARecv(clientInfo->sclient, &dataBuf, 1, &bytesReceived, &flags, &clientInfo->overlapped, nullptr);
        if (retVal == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING) {  // 如果错误不是WSA_IO_PENDING，表示接收操作未成功启动
                std::cerr << "WSARecv failed with error: " << err << std::endl;
                break;
            }
        }

        // 等待接收完成
        DWORD waitRet = WSAWaitForMultipleEvents(1, &clientInfo->overlapped.hEvent, TRUE, INFINITE, FALSE);
        if (waitRet == WSA_WAIT_FAILED) {
            std::cerr << "WSAWaitForMultipleEvents failed with error: " << WSAGetLastError() << std::endl;
            break;
        }

        // 检查接收操作是否已完成
        WSAGetOverlappedResult(clientInfo->sclient, &clientInfo->overlapped, &bytesReceived, FALSE, &flags);

        // 处理接收到的数据
        if (bytesReceived > 0) {
            clientInfo->buf[bytesReceived] = '\0';
            std::cout << "Received from [" << clientInfo->id << "][" << inet_ntoa(clientInfo->addrClient.sin_addr)
                      << ":" << ntohs(clientInfo->addrClient.sin_port) << "]: " << clientInfo->buf << std::endl;

            if (strcmp(clientInfo->buf, "bye") == 0) {
                send(clientInfo->sclient, "bye", 4, 0);
                std::cout << "Client [" << clientInfo->id << "] disconnected." << std::endl;
                break;
            } else {
                char msg[BUF_SIZE];
                sprintf_s(msg, "Message received: %s", clientInfo->buf);
                send(clientInfo->sclient, msg, strlen(msg), 0);
            }
        } else {
            // 客户端断开连接
            std::cout << "Client [" << clientInfo->id << "] disconnected." << std::endl;
            break;
        }

        // 重置事件对象，准备下一次接收
        WSAResetEvent(clientInfo->overlapped.hEvent);
    }

    // 关闭事件对象
    WSACloseEvent(clientInfo->overlapped.hEvent);

    // 关闭套接字并清理内存
    closesocket(clientInfo->sclient);
    delete clientInfo;

    // 更新连接计数
    connectionMutex.lock();
    connectionCount--;
    std::cout << "Total connections: " << connectionCount.load() << std::endl;
    connectionMutex.unlock();
}

// 键盘输入处理线程
DWORD WINAPI KeyboardThread(LPVOID) {
    char input[BUF_SIZE];
    while (true) {
        std::cin.getline(input, BUF_SIZE);
        if (strcmp(input, "exit") == 0) break;

        connectionMutex.lock();
        for (auto client: clients) {
            send(client->sclient, input, strlen(input), 0);
        }
        connectionMutex.unlock();
    }
    return 0;
}

// 清理工作函数
void Cleanup() {
    // 关闭所有客户端连接
    for (auto client : clients) {
        closesocket(client->sclient);
        delete client;
    }

    // 关闭服务器套接字
    WSACleanup();
}
