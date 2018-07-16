#include <iostream>
#include <fstream>
#include <set>

#include <Winsock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

const int port = 9999;

const int perSize = 1028;

const int MemMaxSize = perSize * 1024 * 200;

template<typename T>
inline T round_up(T t)
{
    return (t + 1023) & (~1023);
}

int main(int argc, char* argv[])
{
    WORD socketVersion = MAKEWORD(2, 2);
    WSADATA wsaData;
    if (WSAStartup(socketVersion, &wsaData) != 0)
    {
        return 0;
    }
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in sin, cli;
    memset(&sin, 0, sizeof sin);
    memset(&cli, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.S_un.S_addr = INADDR_ANY;
    
    BOOL optval = 1;
    int ret = ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof optval);
    if (ret == SOCKET_ERROR)
    {
        cout << "setsockopt failed with errno : " << WSAGetLastError() << endl;
        return -1;
    }

    ret = ::bind(sock, (const sockaddr *)&sin, sizeof sin);
    if (ret == SOCKET_ERROR)
    {
        cout << "Bind failed with errno : " << WSAGetLastError() << endl;
        return -1;
    }

    ret = ::listen(sock, 20);
    if (ret == SOCKET_ERROR)
    {
        cout << "listen failed with errno : " << WSAGetLastError() << endl;
        return -1;
    }

    int cliLen = sizeof cli;
    SOCKET cSock = ::accept(sock, (sockaddr *)&cli, &cliLen);
    if (cSock == SOCKET_ERROR)
    {
        cout << "accept failed with errno : " << WSAGetLastError() << endl;
        return -1;
    }

    int recvBufSize = 128 * 1024 * 1024;
    ret = ::setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char*)&recvBufSize, sizeof recvBufSize);
    if (ret == SOCKET_ERROR)
    {
        cout << "setsockopt failed with errno : " << WSAGetLastError() << endl;
        return -1;
    }

    long long fileSize = -1;

    ret = recv(cSock, (char *)&fileSize, sizeof fileSize, 0);
    if (ret != sizeof fileSize || fileSize == 0)
    {
        cout << "can't recv fileSize" << endl;
        return -1;
    }
    cout << "recv file size " << fileSize << " bytes" << endl;
    
    int packNum = round_up(fileSize) / 1024;
    cout << "Pack number : " << packNum << endl;
    int memSize = packNum * perSize < MemMaxSize ? packNum * perSize : MemMaxSize;
    char *pBuf = new char[memSize];
    if (!pBuf)
    {
        cout << "Allocate memory failed!" << endl;
        return -1;
    }

    ofstream out("out.mp4", ios::trunc | ios::binary | ios::out);
    out.write(pBuf, fileSize);
    out.seekp(0);

    char *pHead = pBuf;
    int seq = -1;
    for (int i = 0; i < packNum; ++i)
    {
        ret = ::recv(cSock, pBuf, perSize, MSG_WAITALL);
        if (ret != perSize)
        {
            cout << "Recv less than 1028" << " ret " << ret <<WSAGetLastError() << endl;
            return -1;
        }
        memcpy(&seq, pBuf, 4);
        cout << "Loop " << i << " recv pack num " << seq << endl;
        pBuf += perSize;
    }
    pBuf = pHead;

    for (int i = 0; i < packNum; ++i)
    {
        out.write(pBuf + 4, 1024);
        pBuf += perSize;
    }

    out.close();
    closesocket(sock);
    WSACleanup();
    return 0;
}