#include <atomic>
#include <iostream>
#include <bits/stdc++.h>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <discovery.hpp>
#include <state.hpp>

using namespace std;
atomic<bool> running(true);

string selfUsername;
Role selfRole;
unordered_map<string, Peer> peers;

void broadcast(int port)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    int broadcastEnable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)); // enable broadcast on the socket

    sockaddr_in addr{}; // IPv4 address
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    while (running)
    {
        string msg = selfUsername + "|" + (selfRole == WRITER ? "W" : "R");
        sendto(sock, msg.c_str(), msg.size(), 0, (sockaddr *)&addr, sizeof(addr));
        this_thread::sleep_for(chrono::seconds(3)); // broadcast after every 3 s
    }
    close(sock);
}

void listen(int port)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY; // accept any incoming address

    bind(sock, (sockaddr *)&addr, sizeof(addr)); // allows to receive msgs from sender

    char buffer[1024];
    while (running)
    {
        sockaddr_in sender{};
        socklen_t senderLen = sizeof(sender);
        int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr *)&sender, &senderLen); // receive from broadcast sender
        if (len > 0)
        {
            buffer[len] = '\0';
            std::string msg(buffer);
            auto sep = msg.find('|');
            if (sep != std::string::npos)
            {
                string uname = msg.substr(0, sep);
                string roleStr = msg.substr(sep + 1);

                if (uname != selfUsername)
                { // ignore self
                    Peer p{uname, inet_ntoa(sender.sin_addr), roleStr == "W" ? WRITER : READER};

                    { // lock scope
                        std::lock_guard<mutex> lock(peerRole);
                        peers[uname] = p;
                    }
                }
            }
        }
    }
    close(sock);
}
// multithreading
void startDiscovery(int port)
{
    thread(broadcast, port).detach();
    thread(listen, port).detach();
}

void stopDiscovery()
{
    running = false;
}