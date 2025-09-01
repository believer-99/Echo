#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
using namespace std;
void connectToPeer(const string &peerUsername, int port);
void startServer(int port);

namespace Connection
{
    // Map of peer identifier -> socket fd (used by transfer to broadcast)
    extern std::unordered_map<std::string, int> peerSockets;
    extern std::mutex socketsMutex;

    // Writer's simple notepad editor that appends lines and syncs
    void runNotepad(const std::string &relpath);
    // Reader-side: start a simple viewer for a shared notepad file
    void startNotepadViewer(const std::string &relpath);
    // Set current notepad path so new connections can be snapshotted
    void setCurrentNotepad(const std::string &relpath);
    // Update full content of notepad file and announce
    void updateNotepadContent(const std::string &relpath, const std::string &content);
    // Announce current snapshot (compute hashes and broadcast FILE_DESC)
    void announceNotepadSnapshot(const std::string &relpath);

    // Connect to all discovered READER peers (no-ops for existing connections)
    void connectAllReaders();

    // Simulated network pause (for testing offline behavior)
    extern std::atomic<bool> networkPaused;
    inline void setNetworkPaused(bool v) { networkPaused.store(v); }
    inline bool isNetworkPaused() { return networkPaused.load(); }
}
