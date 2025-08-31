#pragma once
#include <string>
using namespace std;
void connectToPeer(const string &peerUsername, int port);
void connectionLog(int clientSock, string peerName);
void startServer(int port);
