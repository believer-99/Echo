#include "connection.hpp"
#include "state.hpp"

#include <iostream>
#include <thread>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
using namespace std;

// for reader
void handleConnection(int clientSock, string peerName)
{
    char buffer[1024];
    while (true)
    {
        int len = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
        if (len <= 0)
        {
            // close faulty connection if no data received
            cout << "Connection closed by " << peerName << "\n";
            close(clientSock);
            break;
        }
        // else handle incoming data from the sender
        buffer[len] = '\0';
        cout << "[Update from " << peerName << "]: " << buffer << "\n";
    }
}
// for reader
void startServer(int port)
{
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0)
    {
        perror("socket");
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY; // accept any incoming address

    if (bind(serverSock, (sockaddr *)&addr, sizeof(addr)) < 0) // bind op unsuccessful
    {
        // give error and close faulty connection
        perror("bind");
        close(serverSock);
        return;
    }

    if (listen(serverSock, 5) < 0)
    {
        perror("listen");
        close(serverSock);
        return;
    }

    std::cout << "[*] Server listening on port " << port << "\n";

    while (true)
    {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientSock = accept(serverSock, (sockaddr *)&clientAddr, &clientLen);
        if (clientSock < 0)
        {
            perror("accept");
            continue;
        }

        string peerIP = inet_ntoa(clientAddr.sin_addr);
        cout << "New connection from " << peerIP << "\n";

        // spawn a thread for each connection
        thread(handleConnection, clientSock, peerIP).detach();
    }
}

// for writer
void connectToPeer(const string &peerUsername, int port)
{
    Peer target;
    {
        lock_guard<std::mutex> lock(peerRole);
        if (peers.find(peerUsername) == peers.end())
        {
            cout << "Peer not found: " << peerUsername << "\n";
            return;
        }
        target = peers[peerUsername]; // reading peer
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        // error in creating socket
        perror("socket");
        return;
    }

    sockaddr_in peerAddr{};
    peerAddr.sin_family = AF_INET;
    peerAddr.sin_port = htons(port);
    inet_pton(AF_INET, target.ip.c_str(), &peerAddr.sin_addr); // converts peer address to string

    if (connect(sock, (sockaddr *)&peerAddr, sizeof(peerAddr)) < 0)
    {
        // could not connect signal (socket) to receiver
        perror("connect");
        close(sock);
        return;
    }

    cout << "Connected to " << peerUsername << " (" << target.ip << ")\n";

    // Simple input loop: writer types messages, they are sent
    string line;
    cin.ignore();
    while (true)
    {
        cout << "[Write]> ";
        if (!getline(std::cin, line))
            break;
        if (line == "/quit") // used for exiting from the app
            break;
        send(sock, line.c_str(), line.size(), 0);
    }

    close(sock);
}
