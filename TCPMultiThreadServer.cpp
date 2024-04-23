#include <bits/stdc++.h>
#include <WINSOCK2.H>
#include <tchar.h>
#include <mutex>
#include <vector>
#include <atomic>

struct ClientInfo {
    int id; // �ͻ���Ψһ��ʶ��
    SOCKET sclient; // �ͻ����׽���
    sockaddr_in addrClient; // �ͻ��˵�ַ��Ϣ
};

#define BUF_SIZE 64 // ���建������С
std::mutex connectionMutex; // ���ڱ������Ӽ����Ϳͻ����б�Ļ�����
std::atomic<int> connectionCount(0); // ��ǰ��������ԭ�ӱ����Ա�֤�̰߳�ȫ
std::atomic<int> nextClientId(1); // �������ɿͻ���ID��ԭ�ӱ�������ʼֵΪ1
std::vector<ClientInfo *> clients; // �ͻ����б�

// �̺߳�����������Ӧ�ͻ�������
DWORD WINAPI AnswerThread(LPVOID lparam) {
    auto *clientInfo = (ClientInfo *) lparam;
    char buf[BUF_SIZE];
    int retVal;

    // �ͻ���������Ϣ
    connectionMutex.lock();
    connectionCount++;
    printf("Client [%d][%s:%d] connected. Total connections: %d\n",
           clientInfo->id, inet_ntoa(clientInfo->addrClient.sin_addr),
           ntohs(clientInfo->addrClient.sin_port), connectionCount.load());
    clients.push_back(clientInfo);
    connectionMutex.unlock();

    // ͨ��ѭ��
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

// �������봦���߳�
DWORD WINAPI KeyboardThread(LPVOID) {
    char input[BUF_SIZE];
    while (true) {
        std::cin.getline(input, BUF_SIZE); // �Ӽ��̶�ȡһ������
        if (strcmp(input, "exit") == 0) break; // �������exit���˳�

        connectionMutex.lock();
        for (auto client: clients) {
            send(client->sclient, input, strlen(input), 0); // ���͸����пͻ���
        }
        connectionMutex.unlock();
    }
    return 0;
}

// ������
int _tmain(int argc, _TCHAR *argv[]) {
    WSADATA wsd;
    SOCKET sServer;
    int retVal;

    // ��ʼ��Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
        printf("WSAStartup failed !\n");
        return 1;
    }

    // �����������׽���
    sServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sServer == INVALID_SOCKET) {
        printf("Socket failed !\n");
        WSACleanup();
        return -1;
    }

    // ���׽�������Ϊ������ģʽ
    u_long iMode = 1;
    retVal = ioctlsocket(sServer, FIONBIO, &iMode);
    if (retVal == SOCKET_ERROR) {
        printf("Ioctlsocket failed !\n");
        closesocket(sServer);
        WSACleanup();
        return -1;
    }

    // ���÷�������ַ��Ϣ����
    sockaddr_in addrServ{};
    addrServ.sin_family = AF_INET;
    int port = 9990;
    addrServ.sin_port = htons(port); // �����˿�
    addrServ.sin_addr.S_un.S_addr = INADDR_ANY; // �����κε�ַ

    retVal = bind(sServer, (sockaddr *) &addrServ, sizeof(addrServ));
    if (retVal == SOCKET_ERROR) {
        printf("Bind failed !\n");
        closesocket(sServer);
        WSACleanup();
        return -1;
    }

    // ��ʼ����
    retVal = listen(sServer, SOMAXCONN);
    if (retVal == SOCKET_ERROR) {
        printf("Listen failed !\n");
        closesocket(sServer);
        WSACleanup();
        return -1;
    }

    printf("Server is listening on port %d ...\n", port);
    CreateThread(nullptr, 0, KeyboardThread, nullptr, 0, nullptr); // �������������߳�

    // ѭ���ȴ��ͻ�������
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
        // Ϊ�¿ͻ��˷����ڴ�
        auto *clientInfo = new ClientInfo;
        clientInfo->sclient = sClient;
        clientInfo->addrClient = addrClient;
        clientInfo->id = nextClientId++; // ʹ��ԭ�Ӳ�����ȫ�ط���ͻ���ID

        // �����̴߳���ͻ�������
        CreateThread(nullptr, 0, AnswerThread, clientInfo, 0, nullptr);
    }

    // ����������������
    closesocket(sServer);
    WSACleanup();
    return 0;
}
