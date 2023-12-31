﻿#include<iostream>
#include<WINSOCK2.h>
#include<ws2tcpip.h>
#include<time.h>
#include<fstream>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

const int MAXSIZE = 8192;//传输缓冲区最大长度
const unsigned char SYN = 0x1; //SYN = 1 ACK = 0
const unsigned char ACK = 0x2;//SYN = 0 ACK = 1
const unsigned char ACK_SYN = 0x3;//SYN = 1 ACK = 1
const unsigned char FIN = 0x4;//FIN = 1 ACK = 0
const unsigned char FIN_ACK = 0x5;//FIN = 1 ACK = 1
const unsigned char OVER = 0x7;//结束标志
double MAX_TIME = 0.5 * CLOCKS_PER_SEC;

struct HEADER {
    u_short sum = 0;//校验和 16位
    u_short datasize = 0;//所包含数据长度 16位
    unsigned char flag = 0;//八位，使用后三位，排列是FIN ACK SYN 
    unsigned char SEQ = 0;//八位，传输的序列号，0~255，超过后mod
    HEADER() {
        sum = 0;
        datasize = 0;
        flag = 0;
        SEQ = 0;
    }
};

u_short cksum(u_short* mes, int size) {//计算校验和
    int count = (size + 1) / 2;
    u_short* buf = (u_short*)malloc(size + 1);
    if (buf != 0) {
        memset(buf, 0, size + 1);
        memcpy(buf, mes, size);
    }
    u_long sum = 0;
    while (count--) {
        sum += *buf++;
        if (sum & 0xffff0000) {
            sum &= 0xffff;
            sum++;
        }
    }
    return ~(sum & 0xffff);
}

int Connect(SOCKET& socketClient, SOCKADDR_IN& servAddr, int& servAddrlen){//三次握手建立连接
    HEADER header;
    char* Buffer = new char[sizeof(header)];
    //第一次握手
    header.flag = SYN;
    header.sum = 0;//校验和置0
    u_short temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp;//计算校验和
    memcpy(Buffer, &header, sizeof(header));//将首部放入缓冲区
    if (sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen) == -1){
        return -1;
    }
    clock_t start = clock(); //记录发送第一次握手时间
    u_long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);
    //第二次握手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, &servAddrlen) <= 0){
        if (clock() - start > MAX_TIME){//超时，重新传输第一次握手
            cout << "第一次握手超时，正在进行超时重传" << endl;
            header.flag = SYN;
            header.sum = 0;
            header.sum = cksum((u_short*)&header, sizeof(header));
            memcpy(Buffer, &header, sizeof(header));
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
            start = clock();           
        }
    }
    //进行校验和检验
    memcpy(&header, Buffer, sizeof(header));
    if (header.flag != ACK || !cksum((u_short*)&header, sizeof(header) == 0)){
        cout << "握手发生错误" << endl;
        return -1;
    }
    //进行第三次握手
    header.flag = ACK_SYN;
    header.sum = 0;
    header.sum = cksum((u_short*)&header, sizeof(header));//计算校验和
    if (sendto(socketClient, (char*)&header, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen) == -1){
        return -1;//判断客户端是否打开，-1为未开启发送失败
    }
    cout << "服务器连接成功" << endl;
    return 1;
}

void send_package(SOCKET& socketClient, SOCKADDR_IN& servAddr, int& servAddrlen, char* message, int len, int& order){
    HEADER header;
    char* buffer = new char[MAXSIZE + sizeof(header)];
    header.datasize = len;
    header.SEQ = unsigned char(order);//序列号
    memcpy(buffer, &header, sizeof(header));
    memcpy(buffer + sizeof(header), message, sizeof(header) + len);
    u_short check = cksum((u_short*)buffer, sizeof(header) + len);//计算校验和
    header.sum = check;
    memcpy(buffer, &header, sizeof(header));
    sendto(socketClient, buffer, len + sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
    cout << "Send message " << len << " bytes!" << " flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << " SUM:" << int(header.sum) << endl;
    clock_t start = clock();//记录发送时间
    while (1){//接收确认信息
        u_long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode);
        while (recvfrom(socketClient, buffer, MAXSIZE, 0, (sockaddr*)&servAddr, &servAddrlen) <= 0){
            if (clock() - start > MAX_TIME){
                header.datasize = len;
                header.SEQ = u_char(order);
                header.flag = u_char(0x0);
                memcpy(buffer, &header, sizeof(header));
                memcpy(buffer + sizeof(header), message, sizeof(header) + len);
                u_short check = cksum((u_short*)buffer, sizeof(header) + len);
                header.sum = check;
                memcpy(buffer, &header, sizeof(header));
                sendto(socketClient, buffer, len + sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
                cout << "TIME OUT! ReSend message " << len << " bytes! flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << endl;
                start = clock();
            }
        }
        memcpy(&header, buffer, sizeof(header));//缓冲区接收到信息，读取
        u_short check = cksum((u_short*)&header, sizeof(header));
        if (header.SEQ == u_short(order) && header.flag == ACK){
            cout << "Send has been confirmed! flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << endl;
            break;
        }
        else{
            continue;
        }
    }
    u_long mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode);//改回阻塞模式
}

void send(SOCKET& socketClient, SOCKADDR_IN& servAddr, int& servAddrlen, char* message, int len){
    int packagenum = len / MAXSIZE + (len % MAXSIZE != 0);
    int seqnum = 0;
    for (int i = 0; i < packagenum; i++){
        send_package(socketClient, servAddr, servAddrlen, message + i * MAXSIZE, i == packagenum - 1 ? len - (packagenum - 1) * MAXSIZE : MAXSIZE, seqnum);
        seqnum++;
        if (seqnum > 255){
            seqnum = seqnum - 256;
        }
    }
    HEADER header;
    char* Buffer = new char[sizeof(header)];
    header.flag = OVER;
    header.sum = 0;
    header.sum = cksum((u_short*)&header, sizeof(header));
    memcpy(Buffer, &header, sizeof(header));
    sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
    cout << "发送结束" << endl;
    clock_t start = clock();
    while(1){
        u_long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode);
        while (recvfrom(socketClient, Buffer, MAXSIZE, 0, (sockaddr*)&servAddr, &servAddrlen) <= 0){
            if (clock() - start > MAX_TIME){
                char* Buffer = new char[sizeof(header)];
                header.flag = OVER;
                header.sum = 0;
                header.sum = cksum((u_short*)&header, sizeof(header));
                memcpy(Buffer, &header, sizeof(header));
                sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
                cout << "Time Out! ReSend End!" << endl;
                start = clock();
            }
        }
        memcpy(&header, Buffer, sizeof(header));//缓冲区接收到信息，读取
        u_short check = cksum((u_short*)&header, sizeof(header));
        if (header.flag == OVER){
            cout << "对方已成功接收文件!" << endl;
            break;
        }
        else{
            continue;
        }
    }
    u_long mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode);//改回阻塞模式
}

int disConnect(SOCKET& socketClient, SOCKADDR_IN& servAddr, int& servAddrlen){//四次挥手断开连接
    HEADER header;
    char* Buffer = new char[sizeof(header)];
    //进行第一次挥手
    header.flag = FIN;
    header.sum = 0;//校验和置0
    header.sum = cksum((u_short*)&header, sizeof(header));
    memcpy(Buffer, &header, sizeof(header));//将首部放入缓冲区
    if (sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen) == -1){
        return -1;
    }
    clock_t start = clock(); //记录发送第一次挥手时间
    u_long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);
    //第二次挥手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, &servAddrlen) <= 0){
        if (clock() - start > MAX_TIME){//超时，重新传输第一次挥手
            cout << "第一次挥手超时，正在进行重传" << endl;
            header.flag = FIN;
            header.sum = 0;
            header.sum = cksum((u_short*)&header, sizeof(header));
            memcpy(Buffer, &header, sizeof(header));//将首部放入缓冲区
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
            start = clock();
        }
    }
    //进行校验和检验
    memcpy(&header, Buffer, sizeof(header));
    if (header.flag != ACK || !cksum((u_short*)&header, sizeof(header) == 0)){
        cout << "连接发生错误，程序直接退出！" << endl;
        return -1;
    }
    //第三次挥手
    header.flag = FIN_ACK;
    header.sum = 0;
    header.sum = cksum((u_short*)&header, sizeof(header));//计算校验和
    if (sendto(socketClient, (char*)&header, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen) == -1){
        return -1;
    }
    start = clock();
    //第四次挥手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, &servAddrlen) <= 0){
        if (clock() - start > MAX_TIME){//超时，重新传输第三次挥手    
            cout << "第三次握手超时，正在进行重传" << endl;
            header.flag = FIN;
            header.sum = 0;//校验和置0
            header.sum = cksum((u_short*)&header, sizeof(header));//计算校验和
            memcpy(Buffer, &header, sizeof(header));//将首部放入缓冲区
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
            start = clock();
        }
    }
    cout << "四次挥手结束，连接断开！" << endl;
    return 1;
}

int main(){
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);
    SOCKADDR_IN server_addr;   
    SOCKADDR_IN client_addr;
    SOCKET Client;
    char serverIP[50];
    int server_port;
    char clientIP[50];
    int client_port;
    cout << "请输入目标IP: ";
    cin.getline(serverIP, sizeof(serverIP));
    cout << "请输入目标端口: ";
    cin >> server_port;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, serverIP, &server_addr.sin_addr) <= 0) {
        cerr << "目标IP地址不可用" << endl;
        WSACleanup();
        return 1;
    }
    cin.ignore();
    cout << "请输入本机IP: ";
    cin.getline(clientIP, sizeof(clientIP));
    cout << "请输入本机端口: ";
    cin >> client_port;
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(client_port);
    if (inet_pton(AF_INET, clientIP, &client_addr.sin_addr) <= 0) {
        cerr << "本机IP地址不可用" << endl;
        WSACleanup();
        return 1;
    }
    Client = socket(AF_INET, SOCK_DGRAM, 0);
    bind(Client, (SOCKADDR*)&client_addr, sizeof(client_addr));
    int len = sizeof(server_addr);
    if (Connect(Client, server_addr, len) == -1){
        return 0;
    }
    string filename;
    cout << "请输入文件名称:";
    cin >> filename;
    ifstream fin(filename.c_str(), ifstream::binary);
    char* buffer = new char[100000000];
    int index = 0;
    unsigned char temp = fin.get();
    while (fin){
        buffer[index++] = temp;
        temp = fin.get();
    }
    fin.close();
    send(Client, server_addr, len, (char*)(filename.c_str()), filename.length());
    clock_t start = clock();
    send(Client, server_addr, len, buffer, index);
    clock_t end = clock();
    cout << "传输总时间为:" << (end - start) / CLOCKS_PER_SEC << "s" << endl;
    cout << "吞吐率为:" << ((float)index) / ((end - start) / CLOCKS_PER_SEC) << "byte/s" << endl;
    disConnect(Client, server_addr, len);
    delete[] buffer;
    WSACleanup();
    system("pause");
    return 0;
}