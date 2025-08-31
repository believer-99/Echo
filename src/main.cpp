#include "discovery.hpp"
#include "connection.hpp"
#include "state.hpp"

#include <iostream>
#include <string>
#include <thread>
using namespace std;
int main()
{
    cout << "Enter username: ";
    cin >> selfUsername;

    char role;
    cout << "Choose role (W=writer, R=reader): ";
    cin >> role;
    selfRole = (role == 'W') ? WRITER : READER;

    int discoveryPort = 50000;       // static as it is free and acn be used
    int tcpPort = discoveryPort + 1; // """"

    // Start peer discovery
    startDiscovery(discoveryPort);

    // If this peer is READER, start a server thread
    if (selfRole == "READER")
    {
        thread([tcpPort]()
               { startServer(tcpPort); })
            .detach();
    }

    cout << "Type commands (list, connect <username>, quit):\n";

    string cmd;
    while (true)
    {
        cout << "> ";
        cin >> cmd;

        if (cmd == "list")
        {
            lock_guard<std::mutex> lock(peerRole);
            cout << "Discovered peers:\n";
            for (auto &kv : peers)
            {
                Peer p = kv.second;
                cout << " - " << p.username
                     << " [" << p.ip << "] "
                     << (p.role == WRITER ? "Writer" : "Reader") << "\n";
            }
        }
        else if (cmd == "connect")
        {
            if (selfRole == "READER")
            {
                cout << "Only a sender can connect to the reader , not vice versa.\n";
                continue;
            }
            string uname;
            cin >> uname;
            connectToPeer(uname, tcpPort);
        }
        else if (cmd == "quit")
        {
            stopDiscovery();
            break;
        }
        else
        {
            cout << "Unknown command\n";
        }
    }

    return 0;
}