#include <iostream>
#include <cstring>
#include <WinSock2.h>
#include <ws2tcpip.h>
#pragma comment(lib,"ws2_32.lib")   
using namespace std;

#define PORT 8000  // 服务器端口
#define BufSize 1024  // 缓冲区大小
SOCKET client; 

// 接收消息线程
DWORD WINAPI receive(){
    while (1){
        char buffer[BufSize] = {};// 接收数据缓冲区
        int receivebuf = recv(client, buffer, sizeof(buffer), 0);//接收到的字节数
        if (receivebuf > 0){
            cout << "Client " << buffer << endl;
        }
        else{
            cout << "You have disconnected" << endl;
            break;
        }
    }
    return 0;
}
int main(){
    // 初始化DLL
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0){
        cout << "Error in initializing " << WSAGetLastError();
        cout << endl;
        exit(EXIT_FAILURE);
    }
    // 创建客户端套接字
    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET){
        cout << "Error in creating socket " << WSAGetLastError();
        exit(EXIT_FAILURE);
    }
    // 设置服务器地址
    SOCKADDR_IN servAddr; // 服务器地址
    servAddr.sin_family = AF_INET; // 地址类型
    servAddr.sin_port = htons(PORT); // 服务器端口号
    if (inet_pton(AF_INET, "127.0.0.1", &(servAddr.sin_addr)) != 1){
        cout << "Error in inet_pton " << WSAGetLastError();
        exit(EXIT_FAILURE);
    }
    // 向服务器发起连接请求
    if (connect(client, (SOCKADDR*)&servAddr, sizeof(SOCKADDR)) == SOCKET_ERROR){
        cout << "Error in connecting " << WSAGetLastError();
        exit(EXIT_FAILURE);
    }
    // 创建接收消息线程
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)receive, NULL, 0, 0);
    char message[BufSize] = {};
    cout << "Let's chat,please enter a message or enter 'exit' to leave the window" << endl;
    // 发送消息
    while (1){
        cin.getline(message, sizeof(message));
        if (strcmp(message, "exit") == 0){ // 输入 "exit" 退出        
            break;
        }
        send(client, message, sizeof(message), 0); // 发送消息
    }
    closesocket(client);
    WSACleanup();
    system("pause");
    return 0;
}