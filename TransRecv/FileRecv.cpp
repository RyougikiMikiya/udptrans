#include <iostream>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>
#include <algorithm>

#include <cassert>

#include <Winsock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

const int port = 9999;

const int perSize = 1028;

const int MemMaxSize = perSize * 1024 * 200;


struct RecvWork
{
    RecvWork(SOCKET s, int packnum, char *buf, int maxSize)
        :sock(s), packNum(packnum), pBuf(buf), pBufMaxSize(maxSize), done(false)
    {}

    SOCKET sock;
    int packNum;
    char *pBuf;
    int pBufMaxSize;
    bool done;
};

void RecvThread(RecvWork *work)
{
    if (work == nullptr)
        return;
    if (work->pBuf == nullptr)
        return;
    if (work->done)
        return;

    set<int> TotalSequence, curSeq;
    for (int i = 0; i < work->packNum; ++i)
    {
        TotalSequence.insert(i);
    }
    assert(TotalSequence.size() == work->packNum);

    char *pHead = work->pBuf;
    int seq = -1;
    int ret;

    do
    {
        ret = ::recv(work->sock, (char*)&seq, 4, 0);
        if (ret != 4)
        {
            cout << "Recv less than 4" << " ret " << ret << WSAGetLastError() << endl;
            return;
        }
        cout << " recv pack num " << seq << endl;

        curSeq.insert(seq);

        ret = ::recv(work->sock, work->pBuf + seq * 1024, 1024, 0);
        if (ret != 1024)
        {
            cout << "Recv less than 4" << " ret " << ret << WSAGetLastError() << endl;
            return;
        }

    } while (curSeq.size() == work->packNum);

    work->done = true;
    return;
}


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
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);

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

    //ret = ::listen(sock, 20);
    //if (ret == SOCKET_ERROR)
    //{
    //    cout << "listen failed with errno : " << WSAGetLastError() << endl;
    //    return -1;
    //}

    //int cliLen = sizeof cli;
    //SOCKET cSock = ::accept(sock, (sockaddr *)&cli, &cliLen);
    //if (cSock == SOCKET_ERROR)
    //{
    //    cout << "accept failed with errno : " << WSAGetLastError() << endl;
    //    return -1;
    //}

    int recvBufSize = 128 * 1024 * 1024;
    ret = ::setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char*)&recvBufSize, sizeof recvBufSize);
    if (ret == SOCKET_ERROR)
    {
        cout << "setsockopt failed with errno : " << WSAGetLastError() << endl;
        return -1;
    }

    long long fileSize = -1;

    ret = recv(sock, (char *)&fileSize, sizeof fileSize, 0);
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

    ostringstream cmd;
    string fileName("out.mp4");
    cmd << "fsutil file createnew " << fileName << " " << fileSize;
    system(cmd.str().c_str());

    ofstream out(fileName, ios::binary | ios::out);
    if (!out)
    {
        cout << "can't open " << fileName << endl;
        return -1;
    }

    RecvWork work(sock, packNum, pBuf, memSize);
    std::thread recvThread(RecvThread, &work);
    recvThread.join();

    if (!work.done)
        return -1;
    out.write(pBuf, fileSize);

    out.close();
    closesocket(sock);
    WSACleanup();
    return 0;
}