#include "include/connection.hpp"
#include "include/state.hpp"
#include "transfer.hpp"
#include "metadata.h"
#ifdef ECHO_ENABLE_UI
#include "include/ui.hpp"
#endif
#include <fstream>
#include <sys/stat.h>
#include <openssl/sha.h>

#include <iostream>
#include <thread>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <atomic>
using namespace std;

// socket registry used by transfer layer
namespace Connection
{
    std::unordered_map<std::string, int> peerSockets;
    std::mutex socketsMutex;
}
void Connection::connectAllReaders()
{
    // Connect to any discovered READER peers that aren't yet connected
    vector<pair<string, int>> toConnect;
    {
        lock_guard<std::mutex> lock(peerRole);
        for (auto &kv : peers)
        {
            const Peer &p = kv.second;
            if (p.role != READER || p.tcpPort <= 0)
                continue;
            string key = p.ip + ":" + to_string(p.tcpPort);
            lock_guard<std::mutex> lk2(Connection::socketsMutex);
            if (Connection::peerSockets.find(key) == Connection::peerSockets.end())
                toConnect.emplace_back(p.username, p.tcpPort);
        }
    }
    for (auto &item : toConnect)
    {
        // connect by username; port is looked up inside connectToPeer
        connectToPeer(item.first, item.second);
    }
}

// Track current notepad path (writer side)
static std::string &currentNotepadPath()
{
    static std::string path;
    return path;
}

// Forward declaration
static MetadataStore &readerStore();

// Helper to compute current FileMeta for a file
static FileMeta computeFileMeta(const std::string &relpath)
{
    FileMeta m;
    m.chunk_sz = DEFAULT_CHUNK_SZ;
    m.mtime = time(nullptr);
    std::string full = std::string("./") + relpath;
    struct stat st;
    if (stat(full.c_str(), &st) == 0)
        m.size = (size_t)st.st_size;
    else
        m.size = 0;
    FILE *f = fopen(full.c_str(), "rb");
    if (f)
    {
        std::vector<uint8_t> buf(m.chunk_sz);
        m.hashes.clear();
        while (true)
        {
            size_t r = fread(buf.data(), 1, m.chunk_sz, f);
            if (r == 0)
                break;
            unsigned char d[32];
            SHA256(buf.data(), r, d);
            char hex[65];
            for (int i = 0; i < 32; i++)
                sprintf(hex + 2 * i, "%02x", d[i]);
            m.hashes.emplace_back(hex, 64);
            if (r < m.chunk_sz)
                break;
        }
        fclose(f);
    }
    // version from store if present
    auto &store = readerStore();
    FileMeta old;
    if (store.get(relpath, old))
        m.version = old.version + 1;
    else
        m.version = 1;
    // store latest
    store.put(relpath, m);
    return m;
}

// for reader
static bool recv_all(int fd, void *buf, size_t n)
{
    size_t got = 0;
    uint8_t *p = (uint8_t *)buf;
    while (got < n)
    {
        ssize_t r = recv(fd, p + got, n - got, 0);
        if (r <= 0)
            return false;
        got += (size_t)r;
    }
    return true;
}

static MetadataStore &readerStore()
{
    static MetadataStore store("snapshot.txt", "wal.log");
    static bool inited = false;
    if (!inited)
    {
        store.load();
        inited = true;
    }
    return store;
}

void handleConnection(int clientSock, string peerKey)
{
    while (true)
    {
        uint32_t nbe;
        if (!recv_all(clientSock, &nbe, 4))
        {
            cout << "Connection closed by " << peerKey << "\n";
            {
                lock_guard<std::mutex> lk(Connection::socketsMutex);
                Connection::peerSockets.erase(peerKey);
            }
            close(clientSock);
            break;
        }
        uint32_t n = ntohl(nbe);
        if (n == 0 || n > (16 * 1024 * 1024))
        { // sanity
            continue;
        }
        std::vector<uint8_t> frame(n);
        if (!recv_all(clientSock, frame.data(), n))
            continue;
        uint8_t type = frame[0];
        std::vector<uint8_t> payload;
        if (n > 1)
            payload.assign(frame.begin() + 1, frame.end());
        // route frame
        Transfer::processIncomingFrame(clientSock, type, payload, readerStore(), ".");
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
        int peerPort = ntohs(clientAddr.sin_port);
        string key = peerIP + ":" + to_string(peerPort);
        cout << "New connection from " << key << "\n";

        {
            lock_guard<std::mutex> lk(Connection::socketsMutex);
            Connection::peerSockets[key] = clientSock;
        }

        // spawn a thread for each connection
        thread(handleConnection, clientSock, key).detach();
    }
}

// for writer
void connectToPeer(const string &peerUsername, int /*port*/)
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
    int port = target.tcpPort > 0 ? target.tcpPort : 0;
    if (port == 0)
    {
        cout << "Peer does not advertise a TCP port yet.\n";
        close(sock);
        return;
    }
    peerAddr.sin_port = htons(port);
    inet_pton(AF_INET, target.ip.c_str(), &peerAddr.sin_addr); // converts peer address to string

    if (connect(sock, (sockaddr *)&peerAddr, sizeof(peerAddr)) < 0)
    {
        // could not connect signal (socket) to receiver
        perror("connect");
        close(sock);
        return;
    }

    cout << "Connected to " << peerUsername << " (" << target.ip << ":" << port << ")\n";

    {
        lock_guard<std::mutex> lk(Connection::socketsMutex);
        string key = target.ip + ":" + to_string(port);
        Connection::peerSockets[key] = sock;
    }

    // If a notepad is already open, announce to this newly connected peer
    if (!currentNotepadPath().empty())
    {
        const auto &np = currentNotepadPath();
        Transfer::announceOpenNotepad(np);
        FileMeta m = computeFileMeta(np);
        Transfer::announceChangeToSocket(sock, np, m);
    }

    // Receiver loop to handle incoming frames (e.g., GET_CHUNKS from reader)
    std::thread([sock]()
                {
        while (true)
        {
            uint32_t nbe; if (!recv_all(sock, &nbe, 4)) break; uint32_t n = ntohl(nbe);
            if (n == 0 || n > (16*1024*1024)) break;
            std::vector<uint8_t> frame(n);
            if (!recv_all(sock, frame.data(), n)) break;
            uint8_t type = frame[0];
            std::vector<uint8_t> payload;
            if (n > 1) payload.assign(frame.begin()+1, frame.end());
            Transfer::processIncomingFrame(sock, type, payload, readerStore(), ".");
        }
        close(sock); })
        .detach();
}

// Simple notepad: writer appends lines to a common file and announces diffs
void Connection::runNotepad(const std::string &relpath)
{
    auto &store = readerStore();
    std::string full = std::string("./") + relpath;
    std::ofstream ofs(full, std::ios::app);
    if (!ofs.is_open())
    {
        std::cerr << "Failed to open notepad file: " << full << "\n";
        return;
    }

    // Connect to readers, then notify to open the viewer for this notepad
    Connection::connectAllReaders();
    Transfer::announceOpenNotepad(relpath);

    // Mark current notepad and notify readers to open the viewer
    currentNotepadPath() = relpath;
    Transfer::announceOpenNotepad(relpath);

    // Announce current content immediately so the reader sees it even before first keystroke
    announceNotepadSnapshot(relpath);

    std::cout << "[Notepad] Type lines. /quit to exit.\n";
    std::string line;
    while (true)
    {
        std::cout << "> ";
        if (!std::getline(std::cin, line))
            break;
        if (line == "/quit")
            break;
        ofs << line << "\n";
        ofs.flush();
        updateNotepadContent(relpath, ""); // file already written by ofs; this will compute and announce
    }
}

void Connection::setCurrentNotepad(const std::string &relpath)
{
    currentNotepadPath() = relpath;
}

void Connection::updateNotepadContent(const std::string &relpath, const std::string &content)
{
    std::string full = std::string("./") + relpath;
    if (!content.empty())
    {
        std::ofstream out(full, std::ios::binary | std::ios::trunc);
        out.write(content.data(), (std::streamsize)content.size());
        out.flush();
    }
    FileMeta m = computeFileMeta(relpath);
    Transfer::announceChange(relpath, m);
}

void Connection::announceNotepadSnapshot(const std::string &relpath)
{
    FileMeta m = computeFileMeta(relpath);
    Transfer::announceChange(relpath, m);
}

// Reader: simple notepad viewer that periodically prints file contents
void Connection::startNotepadViewer(const std::string &relpath)
{
#ifdef ECHO_ENABLE_UI
    ui_start_reader(relpath);
#else
    static std::atomic<bool> running{false};
    if (running.exchange(true))
        return; // already running
    std::thread([relpath]()
                {
        std::string full = std::string("./") + relpath;
        std::string last;
        while (true) {
            std::ifstream ifs(full);
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            if (content != last) {
                std::cout << "\n[Notepad Viewer] " << relpath << "\n" << content << "\n> ";
                last = content;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } })
        .detach();
#endif
}
