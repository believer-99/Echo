// transfer.cpp
#include "transfer.hpp"
#include "metadata.h"
#include "state.h"
#include "include/connection.hpp"
#include <atomic>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <sys/stat.h>
#include <fcntl.h>
#include <unordered_map>
#include <unordered_set>

static inline uint64_t htonll(uint64_t x)
{
    static const int num = 42;
    if (*(const char *)&num == 42)
    { // little endian
        return (((uint64_t)htonl(x & 0xFFFFFFFFULL)) << 32) | htonl(x >> 32);
    }
    else
    {
        return x;
    }
}
static inline uint64_t ntohll(uint64_t x) { return htonll(x); }

// framing helpers
static bool send_all(int fd, const void *buf, size_t n)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t s = 0;
    while (s < n)
    {
        ssize_t w = send(fd, p + s, n - s, 0);
        if (w <= 0)
            return false;
        s += w;
    }
    return true;
}
static bool send_frame(int fd, uint8_t type, const vector<uint8_t> &payload)
{
    uint32_t len = htonl((uint32_t)(1 + payload.size()));
    if (!send_all(fd, &len, 4))
        return false;
    if (!send_all(fd, &type, 1))
        return false;
    if (payload.size())
        return send_all(fd, payload.data(), payload.size());
    return true;
}
void Transfer::announceOpenNotepad(const std::string &path)
{
    std::vector<uint8_t> payload;
    uint32_t pl = htonl((uint32_t)path.size());
    payload.insert(payload.end(), (uint8_t *)&pl, (uint8_t *)&pl + 4);
    payload.insert(payload.end(), path.begin(), path.end());
    std::lock_guard<std::mutex> lk(Connection::socketsMutex);
    for (auto &kv : Connection::peerSockets)
    {
        send_frame(kv.second, MT_OPEN_NOTEPAD, payload);
    }
}

// serialize FILE_DESC: path_len|path|ver(u64)|size(u64)|chunk_sz(u32)|n_hashes(u32)|[hlen(u32)|hashbytes...]
static vector<uint8_t> build_file_desc_payload(const string &path, const FileMeta &m)
{
    vector<uint8_t> out;
    auto put_u32 = [&](uint32_t v)
    { uint32_t x=htonl(v); uint8_t*p=(uint8_t*)&x; out.insert(out.end(),p,p+4); };
    auto put_u64 = [&](uint64_t v)
    { uint64_t x=htonll(v); uint8_t*p=(uint8_t*)&x; out.insert(out.end(),p,p+8); };
    put_u32((uint32_t)path.size());
    out.insert(out.end(), path.begin(), path.end());
    put_u64(m.version);
    put_u64(m.size);
    put_u32((uint32_t)m.chunk_sz);
    put_u32((uint32_t)m.hashes.size());
    for (auto &h : m.hashes)
    {
        put_u32((uint32_t)h.size());
        out.insert(out.end(), h.begin(), h.end());
    }
    return out;
}

// announceChange called by watcher after metadata.put
bool Transfer::announceChangeToSocket(int sockfd, const string &path, const FileMeta &m)
{
    auto payload = build_file_desc_payload(path, m);
    return send_frame(sockfd, MT_FILE_DESC, payload);
}

// announce to all connected peers (we assume connection module gives list of sockets)
void Transfer::announceChange(const string &path, const FileMeta &m)
{
    // get sockets from ConnectionManager (you need to store them). For demo, we'll broadcast to all peers in a maintained map of sockets.
    lock_guard<mutex> lk(Connection::socketsMutex);
    for (auto &kv : Connection::peerSockets)
    {
        int sock = kv.second;
        announceChangeToSocket(sock, path, m);
    }
}

// Process an incoming frame on reader side (called from connection's handler)
void Transfer::processIncomingFrame(int sockfd, uint8_t type, const vector<uint8_t> &payload, MetadataStore &store, const string &basedir)
{
    struct RecvState
    {
        uint32_t total_chunks{0};
        uint32_t received{0};
        size_t chunk_sz{DEFAULT_CHUNK_SZ};
        uint64_t size{0};
    };
    static std::unordered_map<std::string, RecvState> recvStates;

    if (type == MT_OPEN_NOTEPAD)
    {
        // payload: path_len|path
        size_t off = 0;
        const uint8_t *p = payload.data();
        if (payload.size() < 4)
            return;
        uint32_t pathlen;
        memcpy(&pathlen, p, 4);
        pathlen = ntohl(pathlen);
        off = 4;
        if (off + pathlen > payload.size())
            return;
        string path((char *)(p + off), pathlen);
        // Launch viewer on reader side
        Connection::startNotepadViewer(path);
        return;
    }
    else if (type == MT_FILE_DESC)
    {
        // parse and request missing chunks
        size_t off = 0;
        const uint8_t *p = payload.data();
        auto get_u32 = [&]()
        { uint32_t v; memcpy(&v,p+off,4); off+=4; return ntohl(v); };
        auto get_u64 = [&]()
        { uint64_t v; uint64_t x; memcpy(&x,p+off,8); off+=8; return ntohll(x); };
        uint32_t pathlen = get_u32();
        string path((char *)(p + off), pathlen);
        off += pathlen;
        uint64_t ver = get_u64();
        uint64_t size = get_u64();
        uint32_t csz = get_u32();
        uint32_t n_hash = get_u32();
        vector<string> hashes;
        for (uint32_t i = 0; i < n_hash; i++)
        {
            uint32_t hlen = get_u32();
            string h((char *)(p + off), hlen);
            off += hlen;
            hashes.push_back(h);
        }
        // compare local meta
        FileMeta local;
        vector<uint32_t> missing;
        if (!store.get(path, local))
        {
            for (uint32_t i = 0; i < hashes.size(); ++i)
                missing.push_back(i);
        }
        else
        {
            for (uint32_t i = 0; i < hashes.size(); ++i)
            {
                if (i >= local.hashes.size() || local.hashes[i] != hashes[i])
                    missing.push_back(i);
            }
        }
        // record expected state for finalize
        recvStates[path] = RecvState{(uint32_t)hashes.size(), 0, (size_t)csz, (uint64_t)size};

        if (missing.empty())
        {
            // update meta if version ahead
            FileMeta m;
            m.version = ver;
            m.size = size;
            m.mtime = time(nullptr);
            m.chunk_sz = csz;
            m.hashes = hashes;
            store.put(path, m);
            return;
        }
        // build GET_CHUNKS payload: path_len|path|nidx|idx1|idx2...
        vector<uint8_t> req;
        uint32_t pathl_be = htonl((uint32_t)path.size());
        req.insert(req.end(), (uint8_t *)&pathl_be, (uint8_t *)&pathl_be + 4);
        req.insert(req.end(), path.begin(), path.end());
        uint32_t nidx_be = htonl((uint32_t)missing.size());
        req.insert(req.end(), (uint8_t *)&nidx_be, (uint8_t *)&nidx_be + 4);
        for (auto idx : missing)
        {
            uint32_t x = htonl(idx);
            req.insert(req.end(), (uint8_t *)&x, (uint8_t *)&x + 4);
        }
        // send GET_CHUNKS on sockfd
        send_frame(sockfd, MT_GET_CHUNKS, req);
    }
    else if (type == MT_PUT_CHUNK)
    {
        // payload: path_len|path|idx|len|data...
        size_t off = 0;
        const uint8_t *p = payload.data();
        auto get_u32 = [&]()
        { uint32_t v; memcpy(&v,p+off,4); off+=4; return ntohl(v); };
        uint32_t pathlen = get_u32();
        string path((char *)(p + off), pathlen);
        off += pathlen;
        uint32_t idx = get_u32();
        uint32_t len = get_u32();
        const uint8_t *data = p + off;
        off += len;
        // write to temp file at offset idx*chunk_sz
        string full = basedir + "/" + path;
        // ensure directory exists (naive)
        size_t pos = full.find_last_of('/');
        if (pos != string::npos)
        {
            string d = full.substr(0, pos);
            string cur;
            for (size_t i = 0; i < d.size(); ++i)
            {
                cur.push_back(d[i]);
                if (d[i] == '/' || i + 1 == d.size())
                    mkdir(cur.c_str(), 0755);
            }
        }
        string tmp = full + ".part";
        int fd = open(tmp.c_str(), O_CREAT | O_RDWR, 0644);
        if (fd < 0)
            return;
        off_t offp = (off_t)idx * (off_t)DEFAULT_CHUNK_SZ;
        lseek(fd, offp, SEEK_SET);
        write(fd, data, len);
        fsync(fd);
        close(fd);
        // mark receive and finalize if complete
        auto it = recvStates.find(path);
        if (it != recvStates.end())
        {
            it->second.received += 1;
            if (it->second.received >= it->second.total_chunks)
            {
                // finalize
                rename(tmp.c_str(), full.c_str());
                // update metadata entry
                FileMeta m;
                m.size = it->second.size;
                m.mtime = time(nullptr);
                m.chunk_sz = it->second.chunk_sz;
                // we don't have hashes here; keep existing if any
                FileMeta old;
                if (store.get(path, old))
                {
                    m.version = old.version + 1;
                    m.hashes = old.hashes;
                }
                else
                {
                    m.version = 1;
                }
                store.put(path, m);
                recvStates.erase(it);
            }
        }
    }
    else if (type == MT_GET_CHUNKS)
    {
        // writer should handle GET_CHUNKS: parse indices and send PUT_CHUNK(s)
        size_t off = 0;
        const uint8_t *p = payload.data();
        auto get_u32 = [&]()
        { uint32_t v; memcpy(&v, p + off, 4); off += 4; return ntohl(v); };
        uint32_t pathlen = get_u32();
        std::string path((char *)(p + off), pathlen);
        off += pathlen;
        uint32_t nidx = get_u32();
        std::vector<uint32_t> indices;
        indices.reserve(nidx);
        for (uint32_t i = 0; i < nidx; ++i)
            indices.push_back(get_u32());

        // figure out chunk size from metadata
        FileMeta meta;
        size_t csz = DEFAULT_CHUNK_SZ;
        if (store.get(path, meta))
            csz = meta.chunk_sz;

        // open file and send requested chunks
        std::string full = basedir + "/" + path;
        int fd = open(full.c_str(), O_RDONLY);
        if (fd < 0)
            return;
        std::vector<uint8_t> buf(csz);
        for (auto idx : indices)
        {
            off_t offp = (off_t)idx * (off_t)csz;
            if (lseek(fd, offp, SEEK_SET) < 0)
                continue;
            ssize_t r = read(fd, buf.data(), buf.size());
            if (r <= 0)
                continue;
            // build payload: path_len|path|idx|len|data
            std::vector<uint8_t> out;
            uint32_t pl = htonl((uint32_t)path.size());
            out.insert(out.end(), (uint8_t *)&pl, (uint8_t *)&pl + 4);
            out.insert(out.end(), path.begin(), path.end());
            uint32_t be_idx = htonl(idx);
            out.insert(out.end(), (uint8_t *)&be_idx, (uint8_t *)&be_idx + 4);
            uint32_t be_len = htonl((uint32_t)r);
            out.insert(out.end(), (uint8_t *)&be_len, (uint8_t *)&be_len + 4);
            out.insert(out.end(), buf.begin(), buf.begin() + r);
            send_frame(sockfd, MT_PUT_CHUNK, out);
        }
        close(fd);
    }
}
