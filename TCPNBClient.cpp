#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <Winsock2.h>

#pragma comment(lib, "WS2_32.lib")

constexpr size_t BUF_SIZE = 64;  // 定义缓冲区大小

void receiveMessages(SOCKET sHost) {
    std::vector<char> buf(BUF_SIZE, 0);  // 使用vector初始化缓冲区
    int retVal;

    while (true) {
        retVal = recv(sHost, buf.data(), BUF_SIZE - 1, 0);  // 接收数据
        if (retVal > 0) {
            buf[retVal] = '\0';  // 确保字符串正确终止
            std::cout << "Received from server: " << buf.data() << std::endl;
            if (std::string(buf.begin(), buf.begin() + retVal) == "bye") {
                std::cout << "Server has closed the connection." << std::endl;
                break;
            }
        } else if (retVal == 0) {
            std::cout << "Connection closed by the server." << std::endl;
            break;
        } else {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                std::cerr << "recv failed with error: " << err << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

int initializeSocket(SOCKET& sHost, const std::string& ip, const std::string& port) {
    WSADATA wsd;
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
        std::cerr << "WSAStartup failed!" << std::endl;
        return -1;
    }

    sHost = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sHost == INVALID_SOCKET) {
        std::cerr << "socket creation failed!" << std::endl;
        WSACleanup();
        return -1;
    }

    SOCKADDR_IN servAddr;
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = inet_addr(ip.c_str());
    servAddr.sin_port = htons(static_cast<u_short>(std::stoi(port)));

    if (connect(sHost, reinterpret_cast<LPSOCKADDR>(&servAddr), sizeof(servAddr)) == SOCKET_ERROR) {
        std::cerr << "Connect failed!" << std::endl;
        closesocket(sHost);
        WSACleanup();
        return -1;
    }

    std::cout << "Connected to server successfully." << std::endl;
    return 0;
}

void cleanup(SOCKET sHost) {
    closesocket(sHost);
    WSACleanup();
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <Server IP> <Port>" << std::endl;
        return 1;
    }

    SOCKET sHost;
    if (initializeSocket(sHost, argv[1], argv[2]) != 0) {
        return 1;
    }

    std::thread receiverThread(receiveMessages, sHost);
    std::string input;
    while (getline(std::cin, input)) {
        if (send(sHost, input.c_str(), (int)input.size(), 0) == SOCKET_ERROR) {
            std::cerr << "send failed!" << std::endl;
            break;
        }
        if (input == "bye") {
            break;
        }
    }

    receiverThread.join();
    cleanup(sHost);
    return 0;
}
