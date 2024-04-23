#include <iostream>
#include <string>
#include <thread>
#include <Winsock2.h>

#pragma comment(lib, "WS2_32.lib")
#define BUF_SIZE 64  // ���建������С

// ������Ϣ�ĺ����������ڵ������߳���
void receiveMessages(SOCKET sHost) {
    char buf[BUF_SIZE];  // ������Ϣ�Ļ�����
    int retVal;  // recv�����ķ���ֵ

    while (true) {
        ZeroMemory(buf, BUF_SIZE);  // ��ջ�����
        retVal = recv(sHost, buf, BUF_SIZE, 0);  // ��������
        if (retVal > 0) {  // �ɹ����յ�����
            buf[retVal] = '\0';  // ȷ���ַ�����null��ֹ
            std::cout << "Received from server: " << buf << std::endl;  // ��ӡ���յ�����Ϣ
            if (strcmp(buf, "bye") == 0) {  // ����Ƿ���յ��˳�ָ��
                std::cout << "Server has closed the connection." << std::endl;
                break;
            }
        } else if (retVal == 0) {  // ���ӱ��������˹ر�
            std::cout << "Connection closed by the server." << std::endl;
            break;
        } else {  // ������
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {  // ������������
                std::cerr << "recv failed with error: " << err << std::endl;
                break;
            }
            Sleep(100);  // ������ģʽ�£���ʱ�����ݣ��Ժ�����
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <Server IP> <Port>" << std::endl;
        return 1;
    }

    WSADATA wsd;
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {  // ��ʼ��Winsock
        std::cerr << "WSAStartup failed !" << std::endl;
        return 1;
    }

    SOCKET sHost = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);  // �����׽���
    if (sHost == INVALID_SOCKET) {
        std::cerr << "socket failed !" << std::endl;
        WSACleanup();
        return -1;
    }

    SOCKADDR_IN servAddr;  // ��������ַ�ṹ
    servAddr.sin_family = AF_INET;  // ʹ��IPv4��ַ
    servAddr.sin_addr.s_addr = inet_addr(argv[1]);  // IP��ַ
    servAddr.sin_port = htons(static_cast<u_short>(std::atoi(argv[2])));  // �˿ں�

    if (connect(sHost, (LPSOCKADDR) &servAddr, sizeof(servAddr)) == SOCKET_ERROR) {  // �������ӷ�����
        std::cerr << "Connect failed !" << std::endl;
        closesocket(sHost);
        WSACleanup();
        return -1;
    }

    std::cout << "Connected to server successfully." << std::endl;

    std::thread receiverThread(receiveMessages, sHost);  // ����������Ϣ���߳�
    std::string input;
    while (true) {
        std::getline(std::cin, input);  // �ӱ�׼�����ȡ����
        if (send(sHost, input.c_str(), input.length(), 0) == SOCKET_ERROR) {  // �������ݸ�������
            std::cerr << "send failed !" << std::endl;
            break;
        }
        if (input == "bye") {  // ����������"bye"�����˳�
            break;
        }
    }

    receiverThread.join();  // �ȴ������߳̽���
    closesocket(sHost);  // �ر��׽���
    WSACleanup();  // ����Winsock
    return 0;
}
