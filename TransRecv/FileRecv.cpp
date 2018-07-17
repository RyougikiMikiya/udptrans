#include <iostream>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>
#include <algorithm>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>

#include <cassert>

#include <Winsock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

const int UDP_Port = 9999;

const int TCP_Port = 10000;

const int perSize = 1028;

const int MemMaxSize = perSize * 1024 * 200;

mutex flagMutex;
condition_variable flagCV;
bool retranReady = false;

mutex retranMutex;
condition_variable retranCV;
vector<int> retransNum;

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

enum MsgType
{
    MT_RetranRequset = 0xEE,
    MT_RetranResult
};

const int MsgHeader = 0xCAFE0096;

#pragma pack(push, 1)
struct RetransRecvResult
{
    int msgheader;
    MsgType type;
    bool result;
};
#pragma pack(pop)

void RecvThread(RecvWork *work)
{
    if (work == nullptr)
        return;
    if (work->pBuf == nullptr)
        return;
    if (work->done)
        return;

    vector<int> TotalPackNum(work->packNum), curPack;
    set<int> recvedPack;

    for (int i = 0; i < work->packNum; ++i)
    {
        TotalPackNum[i] = i;
    }
    assert(TotalPackNum.size() == work->packNum);
    retransNum.resize(work->packNum);

    char *pHead = work->pBuf;
    int seq = -1;
    int ret;
    
    DWORD recvTimeout = 200;
    ret = ::setsockopt(work->sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&recvTimeout, sizeof(recvTimeout));
    if (ret == SOCKET_ERROR)
    {
        cout << "setsockopt failed with errno : " << WSAGetLastError() << endl;
        return;
    }

    do
    {
        ret = ::recv(work->sock, (char*)&seq, 4, 0);
        if (ret != 4)
        {
            if (ret == WSAETIMEDOUT)
            {
                {
                    sort(curPack.begin(), curPack.end());
                    lock_guard<mutex> lk(retranMutex);
                    retransNum.clear();
                    set_difference(TotalPackNum.begin(), TotalPackNum.end(), curPack.begin(), curPack.end(), retransNum.begin());
                    TotalPackNum = retransNum;
                    curPack.clear();
                }
                retranCV.notify_one();
                assert(retranReady == false);
                unique_lock<mutex> lk(flagMutex);
                //chrono::seconds s;
                if (flagCV.wait_for(lk, 2s, [] { return retranReady == true; }))
                {
                    retranReady = false;
                    continue;
                }
                else
                {
                    cout << "Send Retrans request failed! This time work failed!" << endl;
                    return;
                }
            }
            else
            {
                cout << "Recv less than 4" << " ret " << ret << WSAGetLastError() << endl;
                return;
            }
        }
        cout << " recv pack num " << seq << endl;

        ret = ::recv(work->sock, work->pBuf + seq * 1024, 1024, 0);
        if (ret != 1024)
        {
            if (ret == WSAETIMEDOUT)
            {
                cout << "Recv body time out" << endl;
                continue;
            }
            else
            {
                cout << "Recv less than 1024" << " ret " << ret << WSAGetLastError() << endl;
                return;
            }
        }
        curPack.push_back(seq);
        recvedPack.insert(seq);

    } while (recvedPack.size() == work->packNum);

    work->done = true;
    return;
}

void retransThread(SOCKET sock)
{
REDO:
    unique_lock<mutex> lk(retranMutex);
    retranCV.wait(lk, [] {return !retransNum.empty(); });
    int total = retransNum.size() * sizeof(int);
    int ret = ::send(sock, (const char*)retransNum.data(), total, 0);
    lk.unlock();
    if (ret != total)
    {
        cout << "Send reTran failed" << "ret " << ret << " Err:" << WSAGetLastError() << endl;
        return;
    }

    RetransRecvResult result;
    ret = ::recv(sock, (char*)&result, sizeof(result), MSG_WAITALL);
    if (ret != sizeof(result))
    {
        cout << "Recv reTran failed" << "ret " << ret << " Err:" << WSAGetLastError() << endl;
        return;
    }

    if ( result.msgheader != MsgHeader || result.type != MT_RetranResult )
    {
        cout << "Recv wrong msg" << endl;
        return;
    }
    
    if (result.result == true)
    {
        lock_guard<mutex> lg(flagMutex);
        flagCV.notify_one();
    }
    else
    {
        goto REDO;
    }
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
    SOCKET UDPSock = socket(AF_INET, SOCK_DGRAM, 0);
    SOCKET TCPSock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in sin, tcp_in, cli;
    memset(&sin, 0, sizeof sin);
    memset(&cli, 0, sizeof sin);
    memset(&tcp_in, 0, sizeof tcp_in);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(UDP_Port);
    sin.sin_addr.S_un.S_addr = INADDR_ANY;

    tcp_in.sin_family = AF_INET;
    tcp_in.sin_port = htons(TCP_Port);
    tcp_in.sin_addr.S_un.S_addr = INADDR_ANY;

    BOOL optval = 1;
    int ret = ::setsockopt(TCPSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof optval);
    if (ret == SOCKET_ERROR)
    {
        cout << "setsockopt SO_REUSEADDR failed with errno : " << WSAGetLastError() << endl;
        return -1;
    }

    ret = ::bind(UDPSock, (const sockaddr *)&sin, sizeof sin);
    if (ret == SOCKET_ERROR)
    {
        cout << "Bind UDPSock failed with errno : " << WSAGetLastError() << endl;
        return -1;
    }

    ret = ::bind(TCPSock, (const sockaddr *)&tcp_in, sizeof tcp_in);
    if (ret == SOCKET_ERROR)
    {
        cout << "Bind failed with errno : " << WSAGetLastError() << endl;
        return -1;
    }

    ret = ::listen(TCPSock, 20);
    if (ret == SOCKET_ERROR)
    {
        cout << "listen failed with errno : " << WSAGetLastError() << endl;
        return -1;
    }

    int cliLen = sizeof cli;
    SOCKET cSock = ::accept(TCPSock, (sockaddr *)&cli, &cliLen);
    if (cSock == SOCKET_ERROR)
    {
        cout << "accept failed with errno : " << WSAGetLastError() << endl;
        return -1;
    }

    int recvBufSize = 128 * 1024 * 1024;
    ret = ::setsockopt(UDPSock, SOL_SOCKET, SO_SNDBUF, (const char*)&recvBufSize, sizeof recvBufSize);
    if (ret == SOCKET_ERROR)
    {
        cout << "setsockopt UDPSock failed with errno : " << WSAGetLastError() << endl;
        return -1;
    }

    long long fileSize = -1;

    ret = recv(UDPSock, (char *)&fileSize, sizeof fileSize, 0);
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

    RecvWork work(UDPSock, packNum, pBuf, memSize);
    std::thread recvThread(RecvThread, &work);
    std::thread retransThread(retransThread, TCPSock);
    recvThread.join();

    if (!work.done)
        return -1;
    out.write(pBuf, fileSize);

    out.close();
    closesocket(UDPSock);
    WSACleanup();
    return 0;
}