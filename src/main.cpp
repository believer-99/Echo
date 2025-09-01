#include "include/discovery.hpp"
#include "include/connection.hpp"
#include "include/state.hpp"
#include "include/metadata.hpp"
#include "include/watcher.hpp"
#include "include/ui.hpp"

#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
using namespace std;
int main()
{
    cout << "Enter username: ";
    cin >> selfUsername;

    char role;
    cout << "Choose role (W=writer, R=reader): ";
    cin >> role;
    selfRole = (role == 'W') ? WRITER : READER;

    // Randomize ports for testing; allow override via env
    auto getenv_int = [](const char *k, int defv)
    { const char* v=getenv(k); return v? atoi(v) : defv; };
    srand(time(nullptr) ^ getpid());
    // Use a fixed default for discovery so peers see each other; override with ECHO_DISCOVERY_PORT if needed
    int discoveryPort = getenv_int("ECHO_DISCOVERY_PORT", 45000);
    int tcpPort = getenv_int("ECHO_TCP_PORT", 40000 + (rand() % 10000));

    // Start peer discovery
    startDiscovery(discoveryPort);

    // If this peer is READER, start a server thread
    if (selfRole == READER)
    {
        selfTcpPort = tcpPort;
        thread([tcpPort]()
               { startServer(tcpPort); })
            .detach();
    }

    cout << "Type commands (list, connect <username>, quit):\n";
    cout << "Type commands (list, connect <username>, notepad [name], quit):\n";

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
                     << " [" << p.ip << ":" << p.tcpPort << "] "
                     << (p.role == WRITER ? "Writer" : "Reader") << "\n";
            }
        }
        else if (cmd == "connect")
        {
            if (selfRole == READER)
            {
                cout << "Only a sender can connect to the reader , not vice versa.\n";
                continue;
            }
            string uname;
            cin >> uname;
            connectToPeer(uname, 0);
            // Notepad mode only: no directory watcher
        }
        else if (cmd == "notepad")
        {
            if (selfRole == READER)
            {
                cout << "Notepad is available on writer only.\n";
                continue;
            }
            string name = "notepad.txt";
            if (cin.peek() == ' ')
            {
                cin >> name;
            }
#ifdef ECHO_ENABLE_UI
            // Start writer UI and also open a local reader-style view if needed
            ui_start_writer(name);
#else
            Connection::runNotepad(name);
#endif
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