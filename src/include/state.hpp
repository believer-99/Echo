#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

using namespace std;

enum Role
{
    WRITER,
    READER
};

struct Peer
{
    string username;
    string ip;
    Role role;
    int tcpPort{0};
};

// self details
extern string selfUsername;
extern Role selfRole;
extern int selfTcpPort; // reader's TCP port (0 if writer)

extern unordered_map<string, Peer> peers; // username->peer details
extern mutex peerRole;                    // protects peers map
