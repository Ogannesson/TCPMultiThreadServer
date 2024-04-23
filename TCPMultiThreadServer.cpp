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

// ��������
void ProcessClient(ClientInfo *clientInfo);
DWORD WINAPI KeyboardThread(LPVOID);
void Cleanup();

int main() {
    WSADATA wsaData;
    SOCKET sServer;
    int retVal;

    // ��ʼ��Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed !" << std::endl;
        return 1;
    }

    // �����������׽���
    sServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sServer == INVALID_SOCKET) {
        std::cerr << "Socket failed !" << std::endl;
        WSACleanup();
        return -1;
    }

    // ���÷�������ַ��Ϣ����
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

    // ��ʼ����
    retVal = listen(sServer, SOMAXCONN);
    if (retVal == SOCKET_ERROR) {
        std::cerr << "Listen failed !" << std::endl;
        closesocket(sServer);
        WSACleanup();
        return -1;
    }

    // �������������߳�
    CreateThread(nullptr, 0, KeyboardThread, nullptr, 0, nullptr);

    std::cout << "Server is listening on port " << port << " ..." << std::endl;

    // ѭ���ȴ��ͻ�������
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

        // Ϊ�¿ͻ��˷����ڴ�
        auto *clientInfo = new ClientInfo;
        clientInfo->sclient = sClient;
        clientInfo->addrClient = addrClient;
        clientInfo->id = nextClientId++;
        ZeroMemory(&clientInfo->overlapped, sizeof(clientInfo->overlapped));

        // �����̴߳���ͻ�������
        std::thread(ProcessClient, clientInfo).detach();
    }

    // ����������������
    Cleanup();
    return 0;
}

// �ͻ�����������
void ProcessClient(ClientInfo *clientInfo) {
    // �����¼����󲢹�����OVERLAPPED�ṹ��
    clientInfo->overlapped.hEvent = WSACreateEvent();
    if (clientInfo->overlapped.hEvent == NULL) {
        std::cerr << "WSACreateEvent failed with error: " << WSAGetLastError() << std::endl;
        return;
    }

    // �ͻ���������Ϣ
    connectionMutex.lock();
    connectionCount++;
    std::cout << "Client [" << clientInfo->id << "] connected from "
              << inet_ntoa(clientInfo->addrClient.sin_addr) << ":" << ntohs(clientInfo->addrClient.sin_port)
              << std::endl;
    clients.push_back(clientInfo);
    std::cout << "Total connections: " << connectionCount.load() << std::endl;
    connectionMutex.unlock();

    // ����ͨ��ѭ��
    while (true) {
        DWORD bytesReceived;
        DWORD flags = 0;
        WSABUF dataBuf;
        dataBuf.buf = clientInfo->buf;
        dataBuf.len = BUF_SIZE;

        int retVal = WSARecv(clientInfo->sclient, &dataBuf, 1, &bytesReceived, &flags, &clientInfo->overlapped, nullptr);
        if (retVal == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING) {  // ���������WSA_IO_PENDING����ʾ���ղ���δ�ɹ�����
                std::cerr << "WSARecv failed with error: " << err << std::endl;
                break;
            }
        }

        // �ȴ��������
        DWORD waitRet = WSAWaitForMultipleEvents(1, &clientInfo->overlapped.hEvent, TRUE, INFINITE, FALSE);
        if (waitRet == WSA_WAIT_FAILED) {
            std::cerr << "WSAWaitForMultipleEvents failed with error: " << WSAGetLastError() << std::endl;
            break;
        }

        // �����ղ����Ƿ������
        WSAGetOverlappedResult(clientInfo->sclient, &clientInfo->overlapped, &bytesReceived, FALSE, &flags);

        // ������յ�������
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
            // �ͻ��˶Ͽ�����
            std::cout << "Client [" << clientInfo->id << "] disconnected." << std::endl;
            break;
        }

        // �����¼�����׼����һ�ν���
        WSAResetEvent(clientInfo->overlapped.hEvent);
    }

    // �ر��¼�����
    WSACloseEvent(clientInfo->overlapped.hEvent);

    // �ر��׽��ֲ������ڴ�
    closesocket(clientInfo->sclient);
    delete clientInfo;

    // �������Ӽ���
    connectionMutex.lock();
    connectionCount--;
    std::cout << "Total connections: " << connectionCount.load() << std::endl;
    connectionMutex.unlock();
}

// �������봦���߳�
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

// ����������
void Cleanup() {
    // �ر����пͻ�������
    for (auto client : clients) {
        closesocket(client->sclient);
        delete client;
    }

    // �رշ������׽���
    WSACleanup();
}
