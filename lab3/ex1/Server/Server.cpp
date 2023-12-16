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
const unsigned char OVER = 0x7;//结束标志
double outtime = 0.3 * CLOCKS_PER_SEC;
const int windows = 18; //窗口大小

struct HEADER {
    u_short sum = 0;//16位校验和 
    u_short dtlen = 0;//16位所包含数据长度
    unsigned char flag = 0;//8位，使用后三位，排列是FIN ACK SYN 
    int sequence = 0;
    HEADER() {
        sum = 0;
        dtlen = 0;
        flag = 0;
        sequence = 0;
    }
};

u_short checkedsum(u_short* mes, int size) {//计算校验和
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

int Connect(SOCKET& sockServer, SOCKADDR_IN& ClientAddr, int& ClientAddrLen) {//连接，三次握手
    HEADER header;
    char* Buffer = new char[sizeof(header)];
    //接收第一次握手
    while (true) {
        if (recvfrom(sockServer, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, &ClientAddrLen) == -1) {
            return -1;
        }
        memcpy(&header, Buffer, sizeof(header));
        if (header.flag == SYN && checkedsum((u_short*)&header, sizeof(header)) == 0) {
            break;
        }
    }
    //进行第二次握手，服务端发送ACK
    header.flag = ACK;
    header.sum = 0;
    u_short temp = checkedsum((u_short*)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServer, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1) {
        return -1;
    }
    clock_t start = clock();//记录第二次握手发送时间
    //接收第三次握手
    while (recvfrom(sockServer, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, &ClientAddrLen) <= 0) {
        if (clock() - start > outtime) {
            header.flag = ACK;
            header.sum = 0;
            u_short temp = checkedsum((u_short*)&header, sizeof(header));
            header.flag = temp;
            memcpy(Buffer, &header, sizeof(header));
            if (sendto(sockServer, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1) {
                return -1;
            }
        }
    }
    HEADER temp1;
    memcpy(&temp1, Buffer, sizeof(header));
    if (temp1.flag == ACK_SYN && checkedsum((u_short*)&temp1, sizeof(temp1) == 0)) {
        cout << "shake hand successfully" << endl;
    }
    else {
        return -1;
    }
    return 1;
}

int Receive(SOCKET& sockServ, SOCKADDR_IN& ClientAddr, int& ClientAddrLen, char* message) {
    long int file_length = 0;//文件长度
    HEADER header;
    char* Buffer = new char[MAXSIZE + sizeof(header)];
    int seq = 0;
    queue<char*> packetBuffer;
    while (true) {
        int length = recvfrom(sockServ, Buffer, sizeof(header) + MAXSIZE, 0, (sockaddr*)&ClientAddr, &ClientAddrLen);//接收报文长度
        memcpy(&header, Buffer, sizeof(header));
        //判断是否是结束
        if (header.flag == OVER && checkedsum((u_short*)&header, sizeof(header)) == 0) {
            cout << "receive successfully" << endl;
            break;
        }
        if (header.flag == unsigned char(0) && checkedsum((u_short*)Buffer, length - sizeof(header))) {
            if (seq != header.sequence) {
                // 收到的不是期望的序列号，将该数据包放入缓存，并发送 ACK
                char* temp = new char[length - sizeof(header)];
                memcpy(temp, Buffer + sizeof(header), length - sizeof(header));
                packetBuffer.push(temp);
                cout << "SEQ:" << header.sequence << " has been cached, the message " << length - sizeof(header) << " bytes,Flag:" << int(header.flag) << " SUM:" << int(header.sum) << endl;
                header.flag = ACK;
                header.dtlen = 0;
                header.sum = 0;
                u_short temp1 = checkedsum((u_short*)&header, sizeof(header));
                header.sum = temp1;
                memcpy(Buffer, &header, sizeof(header));
                sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen);
                cout << "Send to Client ACK:" << (int)header.flag << " SEQ:" << header.sequence << endl;
                continue;
            }
            //取出buffer中的内容
            cout << "Recv message " << length - sizeof(header) << " bytes!Flag:" << int(header.flag) << " SEQ : " << header.sequence << " SUM:" << int(header.sum) << endl;
            char* temp = new char[length - sizeof(header)];
            memcpy(temp, Buffer + sizeof(header), length - sizeof(header));
            memcpy(message + file_length, temp, length - sizeof(header));
            file_length = file_length + int(header.dtlen);
            while (!packetBuffer.empty()) {
                char* temp = packetBuffer.front();
                memcpy(message + file_length, temp, length - sizeof(header));
                file_length = file_length + int(header.dtlen);
                packetBuffer.pop();
                seq++;
            }
            //返回ACK
            header.flag = ACK;
            header.dtlen = 0;
            header.sum = 0;
            u_short temp1 = checkedsum((u_short*)&header, sizeof(header));
            header.sum = temp1;
            memcpy(Buffer, &header, sizeof(header));
            sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen);
            cout << "Send to Clinet ACK:" << (int)header.flag << " SEQ:" << header.sequence << endl;
            seq++;
        }
    }
    header.flag = OVER;
    header.sum = 0;
    u_short temp = checkedsum((u_short*)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1) {
        return -1;
    }
    return file_length;
}

int disConnect(SOCKET& sockServer, SOCKADDR_IN& ClientAddr, int& ClientAddrLen) {
    HEADER header;
    //接收第一次挥手信息
    char* Buffer = new char[sizeof(header)];
    while (true) {
        int length = recvfrom(sockServer, Buffer, sizeof(header) + MAXSIZE, 0, (sockaddr*)&ClientAddr, &ClientAddrLen);//接收报文长度
        memcpy(&header, Buffer, sizeof(header));
        if (header.flag == FIN && checkedsum((u_short*)&header, sizeof(header)) == 0) {
            break;
        }
    }
    //进行第二次挥手，服务器端发送ACK
    header.flag = ACK;
    header.sum = 0;
    u_short temp = checkedsum((u_short*)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServer, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1) {
        return -1;
    }
    clock_t start = clock();//记录第二次挥手发送时间
    //接收第三次挥手
    while (recvfrom(sockServer, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, &ClientAddrLen) <= 0) {
        if (clock() - start > outtime) {
            header.flag = ACK;
            header.sum = 0;
            u_short temp = checkedsum((u_short*)&header, sizeof(header));
            header.flag = temp;
            memcpy(Buffer, &header, sizeof(header));
            if (sendto(sockServer, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1) {
                return -1;
            }
        }
    }
    HEADER temp1;
    memcpy(&temp1, Buffer, sizeof(header));
    if (temp1.flag == FIN_ACK && checkedsum((u_short*)&temp1, sizeof(temp1) == 0)) {}
    else {
        return -1;
    }
    //进行第四次挥手，服务器端发送FIN+ACK
    header.flag = FIN_ACK;
    header.sum = 0;
    temp = checkedsum((u_short*)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServer, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1) {
        return -1;
    }
    cout << "wave hand successfully" << endl;
    return 1;
}


int main() {
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);
    SOCKADDR_IN server_addr;
    SOCKET server;
    char serverIP[50];
    int port;
    cout << "Enter server IP address: ";
    cin.getline(serverIP, sizeof(serverIP));
    cout << "Enter server IP port: ";
    cin >> port;
    server_addr.sin_family = AF_INET;//使用IPV4
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, serverIP, &(server_addr.sin_addr)) != 1) {
        cout << "Server IP is unavailable!" << endl;
        exit(EXIT_FAILURE);
    }
    server = socket(AF_INET, SOCK_DGRAM, 0);//sock_dgram-数据报套接字，sock_stream-流式套接字，
    bind(server, (SOCKADDR*)&server_addr, sizeof(server_addr));//绑定套接字，进入监听状态
    cout << "Binding..." << endl;
    int len = sizeof(server_addr);
    //建立连接
    Connect(server, server_addr, len);
    char* filename = new char[20];
    char* data = new char[100000000];
    int namelen = Receive(server, server_addr, len, filename);
    int count = 0;
    int datalen = Receive(server, server_addr, len, data);
    string a;
    for (int i = 0; i < namelen; i++) {
        a = a + filename[i];
    };
    //disConnect(server, server_addr, len);
    ofstream fout(a.c_str(), ofstream::binary);//使用ofstream对象fout打开文件并以二进制方式写入数据，然后循环将接收到的文件数据写入文件
    for (int i = 0; i < datalen; i++) {
        fout << data[i];
    }
    fout.close();
    delete[] filename;
    delete[] data;
    closesocket(server);
    WSACleanup();
    return 0;
}
