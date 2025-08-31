#include "state.hpp"
using namespace std;

string selfUsername;
Role selfRole;
unordered_map<std::string, Peer> peers;
mutex peerRole;
