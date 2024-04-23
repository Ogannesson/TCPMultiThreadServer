#include <bits/stdc++.h>
#include <WINSOCK2.H>
#include <tchar.h>
#include <mutex>
#include <vector>
#include <atomic>

struct ClientInfo {
    int id; // 客户端唯一标识符
    SOCKET sclient; // 客户端套接字
    sockaddr_in addrClient; // 客户端地址信息
};

#define BUF_SIZE 64 // 定义缓冲区大小
std::mutex connectionMutex; // 用于保护连接计数和客户端列表的互斥锁
std::atomic<int> connectionCount(0); // 当前连接数，原子变量以保证线程安全
std::atomic<int> nextClientId(1); // 用于生成客户端ID的原子变量，初始值为1
std::vector<ClientInfo *> clients; // 客户端列表

// 线程函数，用于响应客户端请求
DWORD WINAPI AnswerThread(LPVOID lparam) {
    auto *clientInfo = (ClientInfo *) lparam;
    char buf[BUF_SIZE];
    int retVal;

    // 客户端连接信息
    connectionMutex.lock();
    connectionCount++;
    printf("Client [%d][%s:%d] connected. Total connections: %d\n",
           clientInfo->id, inet_ntoa(clientInfo->addrClient.sin_addr),
           ntohs(clientInfo->addrClient.sin_port), connectionCount.load());
    clients.push_back(clientInfo);
    connectionMutex.unlock();

    // 通信循环
    while (true) {
        ZeroMemory(buf, BUF_SIZE);
        retVal = recv(clientInfo->sclient, buf, BUF_SIZE, 0);
        if (retVal == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                connectionMutex.lock();
                connectionCount--;
                clients.erase(std::remove(clients.begin(), clients.end(), clientInfo), clients.end());
                printf("Receive failed with error: %d\nClient [%d][%s:%d] disconnected, Total connections: %d\n",
                       err, clientInfo->id, inet_ntoa(clientInfo->addrClient.sin_addr),
                       ntohs(clientInfo->addrClient.sin_port), connectionCount.load());
                connectionMutex.unlock();
                break;
            }
            Sleep(100);
            continue;
        } else if (retVal == 0) {
            connectionMutex.lock();
            connectionCount--;
            clients.erase(std::remove(clients.begin(), clients.end(), clientInfo), clients.end());
            printf("Client [%d] disconnected. Total connections: %d\n", clientInfo->id, connectionCount.load());
            connectionMutex.unlock();
            break;
        }

        buf[retVal] = '\0';
        printf("Received from [%d][%s:%d]: %s\n", clientInfo->id, inet_ntoa(clientInfo->addrClient.sin_addr),
               ntohs(clientInfo->addrClient.sin_port), buf);

        if (strcmp(buf, "bye") == 0) {
            send(clientInfo->sclient, "bye", 4, 0);
            connectionMutex.lock();
            connectionCount--;
            clients.erase(std::remove(clients.begin(), clients.end(), clientInfo), clients.end());
            printf("Client [%d][%s:%d] disconnected, Total connections: %d\n", clientInfo->id,
                   inet_ntoa(clientInfo->addrClient.sin_addr), ntohs(clientInfo->addrClient.sin_port),
                   connectionCount.load());
            connectionMutex.unlock();
            break;
        } else {
            char msg[BUF_SIZE];
            sprintf_s(msg, "Message received: %s", buf);
            send(clientInfo->sclient, msg, strlen(msg), 0);
        }
    }

    closesocket(clientInfo->sclient);
    delete clientInfo;
    return 0;
}

// 键盘输入处理线程
DWORD WINAPI KeyboardThread(LPVOID) {
    char input[BUF_SIZE];
    while (true) {
        std::cin.getline(input, BUF_SIZE); // 从键盘读取一行数据
        if (strcmp(input, "exit") == 0) break; // 如果输入exit则退出

        connectionMutex.lock();
        for (auto client: clients) {
            send(client->sclient, input, strlen(input), 0); // 发送给所有客户端
        }
        connectionMutex.unlock();
    }
    return 0;
}

// 主函数
int _tmain(int argc, _TCHAR *argv[]) {
    WSADATA wsd;
    SOCKET sServer;
    int retVal;

    // 初始化Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
        printf("WSAStartup failed !\n");
        return 1;
    }

    // 创建服务器套接字
    sServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sServer == INVALID_SOCKET) {
        printf("Socket failed !\n");
        WSACleanup();
        return -1;
    }

    // 将套接字设置为非阻塞模式
    u_long iMode = 1;
    retVal = ioctlsocket(sServer, FIONBIO, &iMode);
    if (retVal == SOCKET_ERROR) {
        printf("Ioctlsocket failed !\n");
        closesocket(sServer);
        WSACleanup();
        return -1;
    }

    // 设置服务器地址信息并绑定
    sockaddr_in addrServ{};
    addrServ.sin_family = AF_INET;
    int port = 9990;
    addrServ.sin_port = htons(port); // 监听端口
    addrServ.sin_addr.S_un.S_addr = INADDR_ANY; // 监听任何地址

    retVal = bind(sServer, (sockaddr *) &addrServ, sizeof(addrServ));
    if (retVal == SOCKET_ERROR) {
        printf("Bind failed !\n");
        closesocket(sServer);
        WSACleanup();
        return -1;
    }

    // 开始监听
    retVal = listen(sServer, SOMAXCONN);
    if (retVal == SOCKET_ERROR) {
        printf("Listen failed !\n");
        closesocket(sServer);
        WSACleanup();
        return -1;
    }

    printf("Server is listening on port %d ...\n", port);
    CreateThread(nullptr, 0, KeyboardThread, nullptr, 0, nullptr); // 创建键盘输入线程

    // 循环等待客户端连接
    while (true) {
        sockaddr_in addrClient{};
        int addrClientLen = sizeof(addrClient);
        SOCKET sClient = accept(sServer, (sockaddr *) &addrClient, &addrClientLen);
        if (sClient == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                printf("Accept failed with error: %d\n", err);
                break;
            }
            Sleep(100);
            continue;
        }
        // 为新客户端分配内存
        auto *clientInfo = new ClientInfo;
        clientInfo->sclient = sClient;
        clientInfo->addrClient = addrClient;
        clientInfo->id = nextClientId++; // 使用原子操作安全地分配客户端ID

        // 创建线程处理客户端请求
        CreateThread(nullptr, 0, AnswerThread, clientInfo, 0, nullptr);
    }

    // 服务结束后的清理工作
    closesocket(sServer);
    WSACleanup();
    return 0;
}
