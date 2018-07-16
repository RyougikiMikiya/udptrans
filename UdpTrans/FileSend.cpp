#include <iostream>
#include <fstream>

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
    if (argc != 2)
    {
        cout << "para num is not equal = 2" << endl;
        return -1;
    }

    WORD socketVersion = MAKEWORD(2, 2);
    WSADATA wsaData;
    if(WSAStartup(socketVersion, &wsaData) != 0)
    {
        return 0;
    }
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    /*sin.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");*/
    inet_pton(AF_INET, "127.0.0.1", (void*)&sin.sin_addr.S_un.S_addr);

    int sendBufSize = 32 * 1024 * 1024;
    int ret = ::setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char*)&sendBufSize, sizeof sendBufSize);
    if (ret == SOCKET_ERROR)
    {
        cout << "setsockopt failed with errno : " << WSAGetLastError() << endl;
        return -1;
    }

    int len = sizeof(sin);
    ret = ::connect(sock, (const sockaddr *)&sin, len);
    if (ret == SOCKET_ERROR)
    {
        cout << "Connect failed with errno : " << WSAGetLastError() << endl;
        return -1;
    }

    ifstream file(argv[1], ios::ate | ios::binary | ios::in);
    if (!file)
    {
        cout << "Can't open file " << argv[1] << endl;
        return -1;
    }

    auto fileEnd = file.tellg();
    file.seekg(0);
    auto fileStart = file.tellg();
    auto fileSize = fileEnd - fileStart;
    cout << "File " << argv[1] << " size is " << fileSize << " bytes!" << endl;
    int packNum = round_up(fileSize) / 1024;
    cout << "Pack number : " << packNum << endl;
    int memSize = packNum * perSize < MemMaxSize ? packNum * perSize : MemMaxSize;
    char *pBuf = new char[memSize];
    if (!pBuf)
    {
        cout << "Allocate memory failed!" << endl;
        return -1;
    }

    char *pHead = pBuf;
    for (int i = 0; i < packNum; ++i)
    {
        memcpy(pBuf, (const void *)&i, 4);
        pBuf += 4;
        file.read(pBuf, 1024);
        pBuf += 1024;
    }
    pBuf = pHead;
    file.close();

    ret = ::send(sock, (const char *)&fileSize, sizeof fileSize, 0);
    if (ret != sizeof fileSize)
    {
        cout << "Cant send fileSize! Err: " <<  WSAGetLastError() << endl;
        return -1;
    }

    for (int i = 0; i < packNum; ++i)
    {
        ret = ::send(sock, pBuf, perSize, 0);
        if (ret != perSize)
        {
            cout << "Send " << ret << " bytes data!" << WSAGetLastError() <<endl;
            break;
        }
        pBuf += perSize;
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}