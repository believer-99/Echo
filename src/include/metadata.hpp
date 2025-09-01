#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

static const size_t DEFAULT_CHUNK_SZ = 4096;

struct FileMeta
{
    uint64_t version{};
    uint64_t size{};
    uint64_t mtime{};
    size_t chunk_sz{DEFAULT_CHUNK_SZ};
    std::vector<std::string> hashes; // per-chunk hash (hex)
};

class MetadataStore
{
public:
    MetadataStore(const std::string &snapshotFile, const std::string &walFile, size_t maxWalBytes = 1 << 20);
    void load();
    void put(const std::string &path, const FileMeta &m);
    bool get(const std::string &path, FileMeta &out);
    void del(const std::string &path);
    std::vector<std::pair<std::string, FileMeta>> dumpAll();

private:
    std::string snapshotFile;
    std::string walFile;
    size_t maxWalSize;
    std::unordered_map<std::string, FileMeta> files;
    std::mutex mu;
    std::mutex wal_mtx;

    void append_put_wal(const std::string &path, const FileMeta &m);
    void append_del_wal(const std::string &path);
    void snapshot_if_needed();
};
