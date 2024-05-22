#include <iostream>
#include <winsock2.h>
#include <windows.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <memory>
#include <string>
#include <algorithm>

constexpr int BUF_SIZE = 64;

struct ClientInfo {
    int id;
    SOCKET clientsock;
    sockaddr_in addrClient;
    OVERLAPPED overlapped;
    char buf[BUF_SIZE];
};

std::mutex connectionMutex;
std::atomic<int> connectionCount(0);
std::atomic<int> nextClientId(1);
std::vector<std::shared_ptr<ClientInfo>> clients;

void ProcessClient(const std::shared_ptr<ClientInfo> &clientInfo);

DWORD WINAPI KeyboardThread(LPVOID);

void Cleanup();

bool SetupServerSocket(SOCKET &sServer, int port);

int main() {
    WSADATA wsaData;
    SOCKET sServer;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed!" << std::endl;
        return 1;
    }

    if (!SetupServerSocket(sServer, 9990)) {
        WSACleanup();
        return -1;
    }

    CreateThread(nullptr, 0, KeyboardThread, nullptr, 0, nullptr);
    std::cout << "Server is listening on port 9990..." << std::endl;

    while (true) {
        sockaddr_in addrClient{};
        int addrClientLen = sizeof(addrClient);
        SOCKET sClient = accept(sServer, (sockaddr *) &addrClient, &addrClientLen);

        if (sClient == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                Sleep(100);//非阻塞错误，稍后重试
                continue;
            } else if (err == WSAEINTR) {
                std::cerr << "Accept was interrupted by a signal or server shutdown." << std::endl;
                break;//正常退出，因为服务器正在关闭
            } else {
                std::cerr << "Accept failed with error: " << err << std::endl;
                break;//其他错误，退出循环
            }
        }

        auto clientInfo = std::make_shared<ClientInfo>();
        clientInfo->clientsock = sClient;
        clientInfo->addrClient = addrClient;
        clientInfo->id = nextClientId++;
        ZeroMemory(&clientInfo->overlapped, sizeof(clientInfo->overlapped));

        std::thread(ProcessClient, clientInfo).detach();
    }

    Cleanup();
    WSACleanup();
    return 0;
}

void ProcessClient(const std::shared_ptr<ClientInfo> &clientInfo) {
    clientInfo->overlapped.hEvent = WSACreateEvent();
    if (clientInfo->overlapped.hEvent == nullptr) {
        std::cerr << "WSACreateEvent failed with error: " << WSAGetLastError() << std::endl;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(connectionMutex);
        connectionCount++;
        clients.push_back(clientInfo);
    }

    std::cout << "Client [" << clientInfo->id << "] connected from "
              << inet_ntoa(clientInfo->addrClient.sin_addr) << ":" << ntohs(clientInfo->addrClient.sin_port)
              << std::endl << "Total connections: " << connectionCount.load() << std::endl;

    while (true) {
        DWORD bytesReceived;
        DWORD flags = 0;
        WSABUF dataBuf{BUF_SIZE, clientInfo->buf};

        int retVal = WSARecv(clientInfo->clientsock, &dataBuf, 1, &bytesReceived, &flags, &clientInfo->overlapped,
                             nullptr);
        if (retVal == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
            std::cerr << "WSARecv failed with error: " << WSAGetLastError() << std::endl;
            break;
        }

        DWORD waitRet = WSAWaitForMultipleEvents(1, &clientInfo->overlapped.hEvent, TRUE, INFINITE, FALSE);
        if (waitRet == WSA_WAIT_FAILED) {
            std::cerr << "WSAWaitForMultipleEvents failed with error: " << WSAGetLastError() << std::endl;
            break;
        }

        WSAGetOverlappedResult(clientInfo->clientsock, &clientInfo->overlapped, &bytesReceived, FALSE, &flags);
        if (bytesReceived == 0) {
            std::cout << "Client [" << clientInfo->id << "] disconnected." << std::endl;
            break;
        }

        clientInfo->buf[bytesReceived] = '\0';
        std::cout << "Received from [" << clientInfo->id << "]: " << clientInfo->buf << std::endl;

        std::string msg = "Message received: " + std::string(clientInfo->buf);
        send(clientInfo->clientsock, msg.c_str(), (int) msg.length(), 0);
    }

    WSACloseEvent(clientInfo->overlapped.hEvent);
    closesocket(clientInfo->clientsock);
    {
        std::lock_guard<std::mutex> lock(connectionMutex);
        connectionCount--;
        auto iter = std::find_if(clients.begin(), clients.end(),
                                 [&clientInfo](const std::shared_ptr<ClientInfo> &ci) {
                                     return ci->id == clientInfo->id;
                                 });
        if (iter != clients.end()) {
            clients.erase(iter);
        }
        std::cout << "Total connections: " << connectionCount.load() << std::endl;
    }
}

DWORD WINAPI KeyboardThread(LPVOID) {
    std::string input;
    while (true) {
        std::getline(std::cin, input);
        if (input == "exit") {
            Cleanup();
            WSACleanup();
            break;
        } else if (connectionCount == 0) {
            std::cout << "No clients connected." << std::endl;
            continue;
        } else {
            std::lock_guard<std::mutex> lock(connectionMutex);
            std::cout << "Broadcasting message to all clients ..." << std::endl;
            for (auto &client: clients) {
                std::cout << "Sending message to client [" << client->id << "] ..." << std::endl;
                int sendResult = send(client->clientsock, input.c_str(), (int) input.length(), 0);
                if (sendResult == SOCKET_ERROR) {
                    std::cout << "Failed to send message to client [" << client->id << "], error: " << WSAGetLastError()
                              << std::endl;
                } else {
                    std::cout << "Message sent to client [" << client->id << "]: '" << input << "'" << std::endl;
                }
            }
        }
    }
    return 0;
}

void Cleanup() {
    for (auto &client: clients) {
        closesocket(client->clientsock);
        WSACloseEvent(client->overlapped.hEvent);
    }
    clients.clear();
}

bool SetupServerSocket(SOCKET &sServer, int port) {
    sServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sServer == INVALID_SOCKET) {
        std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        return false;
    }

    sockaddr_in addrServ{};
    addrServ.sin_family = AF_INET;
    addrServ.sin_port = htons(port);
    addrServ.sin_addr.S_un.S_addr = INADDR_ANY;

    if (bind(sServer, (sockaddr *) &addrServ, sizeof(addrServ)) == SOCKET_ERROR) {
        std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(sServer);
        return false;
    }

    if (listen(sServer, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed with error: " << WSAGetLastError() << std::endl;
        closesocket(sServer);
        return false;
    }

    return true;
}