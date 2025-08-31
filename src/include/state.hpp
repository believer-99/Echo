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
};
// self details
extern string selfUsername;
extern string selfRole;

extern unordered_map<string, Peer> peers; // username->peer details
extern mutex peerRole;                    // can be either R or W only
