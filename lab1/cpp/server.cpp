#include <iostream>
#include <cstring>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <chrono>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

#define PORT 8000 //服务器端口
#define BufSize 1024 //缓冲区大小
#define Max 6 //最大连接量

int connect_count = 0; //连接数量
int condition[Max] = {}; //连接状态
SOCKET server; //服务器套接字
SOCKET client[Max]; //客户机套接字
// 线程函数，用于处理每个客户端的消息
DWORD WINAPI Threadfun(LPVOID lpParameter){
    // 从传入的参数中获取客户端的索引
    int num = static_cast<int>(reinterpret_cast<intptr_t>(lpParameter));//从参数lpParameter中提取一个整数值
    int rece = 0;       // 用于存储接收到的字节数
    char Rece[BufSize];  // 接收缓冲区
    char Send[BufSize];  // 发送缓冲区
    while (1){
        // 接收来自客户端的消息
        rece = recv(client[num], Rece, sizeof(Rece), 0);
        if (rece > 0){
            // 获取当前时间戳
            auto currentTime = chrono::system_clock::now();
            time_t timestamp = chrono::system_clock::to_time_t(currentTime);
            tm localTime;
            localtime_s(&localTime, &timestamp);
            char timeStr[50];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d--%H:%M:%S", &localTime);
            // 打印接收到的消息和时间戳
            cout << "Client " << client[num] << ": " << Rece << endl;
            cout << timeStr << endl;
            // 格式化要发送的消息
            sprintf_s(Send, sizeof(Send), "%d: %s \n%s ", client[num], Rece, timeStr);
            // 将消息发送给所有连接的客户端
            for (int i = 0; i < Max; i++){
                if (condition[i] == 1){
                    send(client[i], Send, sizeof(Send), 0);
                }
            }
        }
        else{
            if (WSAGetLastError() == 10054){  // 如果客户端主动关闭连接
                auto currentTime = chrono::system_clock::now();
                time_t timestamp = chrono::system_clock::to_time_t(currentTime);
                tm localTime;
                localtime_s(&localTime, &timestamp);
                char timeStr[50];
                strftime(timeStr, sizeof(timeStr), "%Y-%m-d--%H:%M:%S", &localTime);

                cout << "Client " << client[num] << " exit" <<  endl;
                cout << timeStr << endl;

                // 关闭客户端套接字，减少连接数
                closesocket(client[num]);
                connect_count--;
                condition[num] = 0;
                cout << "connect_count: " << connect_count << endl;
                return 0;
            }
            else{ // 接收失败
                cout << "Error in receiving " << WSAGetLastError();
                break;
            }
        }
    }
}
int main(){
    // 初始化Winsock库
    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2){
        cout << "Error in initializing " << WSAGetLastError();
        exit(EXIT_FAILURE);
    }
    // 创建服务器套接字
    server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET){
        cout << "Error in creating socket " << WSAGetLastError();
        exit(EXIT_FAILURE);
    }
    // 设置服务器地址
    SOCKADDR_IN serveraddr;
    SOCKADDR_IN clientaddrs[Max];
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &(serveraddr.sin_addr)) != 1) {
        cout << "Error in inet_pton " << WSAGetLastError();
        exit(EXIT_FAILURE);
    }
    // 绑定服务器套接字
    if (bind(server, (LPSOCKADDR)&serveraddr, sizeof(serveraddr)) == SOCKET_ERROR){
        cout << "Error in binding " << WSAGetLastError();
        exit(EXIT_FAILURE);
    }
    // 设置监听
    if (listen(server, Max) != 0){
        cout << "Error in listening " << WSAGetLastError();
        exit(EXIT_FAILURE);
    }

    cout << "The Server is ready!" << endl;
    cout << "connect_count: " << connect_count << endl;
    while (1){
        if (connect_count < Max){
            int num = 0; //可用的客户端连接
            for (num; num < Max; num++){
                if (condition[num] == 0){
                    break;
                }
            }
            int addrlen = sizeof(SOCKADDR_IN); //接收客户端的地址信息
            client[num] = accept(server, (sockaddr*)&clientaddrs[num], &addrlen); //客户端连接
            if (client[num] == SOCKET_ERROR){
                cout << "The client is failed " << WSAGetLastError();
                closesocket(server);
                WSACleanup();
                exit(EXIT_FAILURE);
            }
            condition[num] = 1; //表示已占用
            connect_count++;
            auto currentTime = chrono::system_clock::now();
            time_t timestamp = chrono::system_clock::to_time_t(currentTime);
            tm localTime;
            localtime_s(&localTime, &timestamp);
            char timeStr[50];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d--%H:%M:%S", &localTime);

            cout << "The Client " << client[num] << " is connected" << endl;
            cout << timeStr << endl;
            cout << "connect_count: " << connect_count << endl;

            HANDLE Thread = CreateThread(NULL, 0, Threadfun, reinterpret_cast<LPVOID>(static_cast<intptr_t>(num)), 0, NULL);//创建新线程
            if (Thread == NULL){
                cout << "The thread is failed " << WSAGetLastError();
                exit(EXIT_FAILURE);
            }
            else{
                CloseHandle(Thread);
            }
        }
        else{//已达最大连接量
            cout << "Server is full!" << endl;
        }
    }
    closesocket(server);
    WSACleanup();
    system("pause");
    return 0;
}