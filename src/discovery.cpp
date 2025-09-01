// discovery.cpp (clean)
#include <atomic>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <thread>
#include <chrono>
#include "include/discovery.hpp"
#include "include/state.hpp"

using namespace std;

extern string selfUsername;
extern Role selfRole;
extern unordered_map<string, Peer> peers;
extern mutex peerRole;
extern int selfTcpPort;

static atomic<bool> running(true);

static void discovery_broadcast(int port)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return;
    int broadcastEnable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("255.255.255.255");
    while (running)
    {
        string msg = selfUsername + "|" + (selfRole == WRITER ? "W" : "R") + "|" + to_string(selfTcpPort);
        sendto(sock, msg.c_str(), msg.size(), 0, (sockaddr *)&addr, sizeof(addr));
        this_thread::sleep_for(chrono::seconds(3));
    }
    close(sock);
}

static void discovery_listen(int port)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return;
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(sock);
        return;
    }
    char buffer[1024];
    while (running)
    {
        sockaddr_in sender{};
        socklen_t senderLen = sizeof(sender);
        int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr *)&sender, &senderLen);
        if (len > 0)
        {
            buffer[len] = '\0';
            string msg(buffer);
            auto s1 = msg.find('|');
            if (s1 != string::npos)
            {
                string uname = msg.substr(0, s1);
                auto s2 = msg.find('|', s1 + 1);
                string roleStr = s2 == string::npos ? msg.substr(s1 + 1) : msg.substr(s1 + 1, s2 - (s1 + 1));
                int tcpPort = 0;
                if (s2 != string::npos)
                    tcpPort = atoi(msg.substr(s2 + 1).c_str());
                if (uname != selfUsername)
                {
                    Peer p{uname, inet_ntoa(sender.sin_addr), roleStr == "W" ? WRITER : READER};
                    p.tcpPort = tcpPort;
                    lock_guard<mutex> lock(peerRole);
                    peers[uname] = p;
                }
            }
        }
    }
    close(sock);
}

void startDiscovery(int port)
{
    thread([port]()
           { discovery_broadcast(port); })
        .detach();
    thread([port]()
           { discovery_listen(port); })
        .detach();
}

void stopDiscovery()
{
    running = false;
}