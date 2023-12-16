#include<iostream>
#include<fstream>
#include<cstring>
#include <queue>
#include<time.h>
#include<WinSock2.h>
#include<ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

const int MAXSIZE = 8192;//传输缓冲区最大长度
const unsigned char SYN = 0x1; //SYN = 1 ACK = 0
const unsigned char ACK = 0x2;//SYN = 0 ACK = 1
const unsigned char ACK_SYN = 0x3;//SYN = 1 ACK = 1
const unsigned char FIN = 0x4;//FIN = 1 ACK = 0
const unsigned char FIN_ACK = 0x5;//FIN = 1 ACK = 0
const unsigned char REPEAT = 0x6;//重复标志
const unsigned char OVER = 0x7;//结束标志
double MAX_TIME = 0.5 * CLOCKS_PER_SEC;
const int WINDOW_SIZE = 10; //窗口大小

struct HEADER {
    u_short sum = 0;//校验和 16位
    u_short datasize = 0;//所包含数据长度 16位
    unsigned char flag = 0;//八位，使用后三位，排列是FIN ACK SYN 
    int SEQ = 0;
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

int Connect(SOCKET& sockServ, SOCKADDR_IN& ClientAddr, int& ClientAddrLen) {
    HEADER header;
    char* Buffer = new char[sizeof(header)];
    //第一次握手
    while (1) {
        if (recvfrom(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, &ClientAddrLen) == -1) {
            return -1;
        }
        memcpy(&header, Buffer, sizeof(header));
        if (header.flag == SYN && cksum((u_short*)&header, sizeof(header)) == 0) {
            break;
        }
    }
    //发送第二次握手信息
    header.flag = ACK;
    header.sum = 0;
    u_short temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1) {
        return -1;
    }
    clock_t start = clock();//记录第二次握手发送时间

    //接收第三次握手
    while (recvfrom(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, &ClientAddrLen) <= 0){
        if (clock() - start > MAX_TIME){
            header.flag = ACK;
            header.sum = 0;
            u_short temp = cksum((u_short*)&header, sizeof(header));
            header.flag = temp;
            memcpy(Buffer, &header, sizeof(header));
            if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1){
                return -1;
            }
            cout << "握手超时，正在进行重传" << endl;
        }
    }
    HEADER temp1;
    memcpy(&temp1, Buffer, sizeof(header));
    if (temp1.flag != ACK_SYN || !cksum((u_short*)&temp1, sizeof(temp1) == 0)) {
        cout << "连接发生错误" << endl;
        return -1;
    }
    return 1;
}

int RecvMessage(SOCKET& sockServ, SOCKADDR_IN& ClientAddr, int& ClientAddrLen, char* message) {
    long int file_length = 0;//文件长度
    HEADER header;
    char* Buffer = new char[MAXSIZE + sizeof(header)];
    int seq = 0;
    queue<char*> packetBuffer;
    while (1) {
        int length = recvfrom(sockServ, Buffer, sizeof(header) + MAXSIZE, 0, (sockaddr*)&ClientAddr, &ClientAddrLen);//接收报文长度
        memcpy(&header, Buffer, sizeof(header));
        if (header.flag == unsigned char(0) && cksum((u_short*)Buffer, length - sizeof(header))) {
            if (seq != header.SEQ) {
                // 收到的不是期望的序列号，将该数据包放入缓存，并发送 ACK
                char* temp = new char[length - sizeof(header)];
                memcpy(temp, Buffer + sizeof(header), length - sizeof(header));
                packetBuffer.push(temp);
                cout << "SEQ:" << header.SEQ << " has been cached, the message " << length - sizeof(header) << " bytes,Flag:" << int(header.flag) <<  " SUM:" << int(header.sum) << endl;
                header.flag = ACK;
                header.datasize = 0;
                header.sum = 0;
                u_short temp1 = cksum((u_short*)&header, sizeof(header));
                header.sum = temp1;
                memcpy(Buffer, &header, sizeof(header));
                sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen);
                cout << "Send to Client ACK:" << (int)header.flag << " SEQ:" << header.SEQ << endl;
                continue;
            }
            //取出buffer中的内容
            cout << "Recv message " << length - sizeof(header) << " bytes!Flag:" << int(header.flag) << " SEQ : " << header.SEQ << " SUM:" << int(header.sum) << endl;
            char* temp = new char[length - sizeof(header)];
            memcpy(temp, Buffer + sizeof(header), length - sizeof(header));
            memcpy(message + file_length, temp, length - sizeof(header));
            file_length = file_length + int(header.datasize);
            while (!packetBuffer.empty()) {
                char* temp = packetBuffer.front();
                memcpy(message + file_length, temp, length - sizeof(header));
                file_length = file_length + int(header.datasize);
                packetBuffer.pop();
                seq++;
            }
            //返回ACK
            header.flag = ACK;
            header.datasize = 0;
            header.sum = 0;
            u_short temp1 = cksum((u_short*)&header, sizeof(header));
            header.sum = temp1;
            memcpy(Buffer, &header, sizeof(header));
            sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen);
            cout << "Send to Clinet ACK:" << (int)header.flag << " SEQ:" << header.SEQ << endl;
            seq++;
        }
        //判断是否是结束
        if (header.flag == OVER && cksum((u_short*)&header, sizeof(header)) == 0) {
            cout << "文件接收完毕" << endl;
            break;
        }
    }
    header.flag = OVER;
    header.sum = 0;
    u_short temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1) {
        return -1;
    }
    return file_length;
}

int disConnect(SOCKET& sockServ, SOCKADDR_IN& ClientAddr, int& ClientAddrLen){
    HEADER header;
    char* Buffer = new char[sizeof(header)];
    while (1){
        int length = recvfrom(sockServ, Buffer, sizeof(header) + MAXSIZE, 0, (sockaddr*)&ClientAddr, &ClientAddrLen);//接收报文长度
        memcpy(&header, Buffer, sizeof(header));
        if (header.flag == FIN && cksum((u_short*)&header, sizeof(header)) == 0){
            break;
        }
    }
    //第二次挥手
    header.flag = ACK;
    header.sum = 0;
    u_short temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1)
    {
        return -1;
    }
    clock_t start = clock();//记录第二次挥手发送时间

    //第三次挥手
    while (recvfrom(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, &ClientAddrLen) <= 0){
        if (clock() - start > MAX_TIME){
            cout << "第二次挥手超时，正在进行重传" << endl;
            header.flag = ACK;
            header.sum = 0;
            u_short temp = cksum((u_short*)&header, sizeof(header));
            header.flag = temp;
            memcpy(Buffer, &header, sizeof(header));
            if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1){
                return -1;
            }          
        }
    }
    HEADER temp1;
    memcpy(&temp1, Buffer, sizeof(header));
    if (temp1.flag != FIN_ACK || !cksum((u_short*)&temp1, sizeof(temp1) == 0)){
        cout << "发生错误" << endl;
        return -1;
    }
    //发送第四次挥手信息
    header.flag = FIN_ACK;
    header.sum = 0;
    temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1){
        cout << "发生错误" << endl;
        return -1;
    }
    cout << "四次挥手结束，连接断开！" << endl;
    return 1;
}

int main(){
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);
    SOCKADDR_IN server_addr;
    SOCKET Server;
    char serverIP[50];
    int port;
    cout << "请输入本服务器IP: ";
    cin.getline(serverIP, sizeof(serverIP));
    cout << "请输入本服务器端口: ";
    cin >> port;
    server_addr.sin_family = AF_INET;//使用IPV4
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, serverIP, &server_addr.sin_addr) <= 0) {
        cerr << "本服务器IP地址不可用" << endl;
        WSACleanup();
        return 1;
    }
    Server = socket(AF_INET, SOCK_DGRAM, 0);
    bind(Server, (SOCKADDR*)&server_addr, sizeof(server_addr));//绑定套接字，进入监听状态
    cout << "进入监听状态，等待客户端连接...." << endl;
    int len = sizeof(server_addr);
    Connect(Server, server_addr, len);
    char* name = new char[20];
    char* data = new char[100000000];
    int namelen = RecvMessage(Server, server_addr, len, name);
    int datalen = RecvMessage(Server, server_addr, len, data);
    string file;
    for (int i = 0; i < namelen; i++){
        file = file + name[i];
    }
    disConnect(Server, server_addr, len);
    ofstream fout(file.c_str(), ofstream::binary);
    for (int i = 0; i < datalen; i++){
        fout << data[i];
    }
    fout.close();
    cout << file <<"已成功下载到本地" << endl;
    delete[] name;
    delete[] data;
    WSACleanup();
    system("pause");
    return 0;
}