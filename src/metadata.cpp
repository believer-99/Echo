// metadata.cpp
#include "metadata.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <chrono>

using namespace std;

// simple helper: split CSV of hashes
static vector<string> split_hashes(const string &s)
{
    vector<string> out;
    size_t i = 0;
    while (i < s.size())
    {
        size_t j = s.find(',', i);
        if (j == string::npos)
        {
            out.push_back(s.substr(i));
            break;
        }
        out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

MetadataStore::MetadataStore(const string &snap, const string &wal, size_t maxWalBytes)
    : snapshotFile(snap), walFile(wal), maxWalSize(maxWalBytes)
{
}

void MetadataStore::load()
{
    lock_guard<mutex> lk(mu);
    files.clear();
    // 1) load snapshot if exists
    ifstream s(snapshotFile);
    if (s)
    {
        string line;
        while (getline(s, line))
        {
            if (line.size() == 0)
                continue;
            if (line.rfind("PUT|", 0) == 0)
            {
                string rest = line.substr(4);
                // parse: path|ver|size|mtime|chunk_sz|h1,h2,...
                vector<string> parts;
                size_t pos = 0;
                for (int k = 0; k < 5; k++)
                {
                    size_t p = rest.find('|', pos);
                    if (p == string::npos)
                    {
                        parts.push_back(rest.substr(pos));
                        pos = rest.size();
                        break;
                    }
                    parts.push_back(rest.substr(pos, p - pos));
                    pos = p + 1;
                }
                if (parts.size() < 5)
                    continue;
                string path = parts[0];
                FileMeta m{};
                m.version = stoull(parts[1]);
                m.size = stoull(parts[2]);
                m.mtime = stoull(parts[3]);
                m.chunk_sz = (size_t)stoul(parts[4]);
                string hashcsv = (pos < rest.size() ? rest.substr(pos) : string());
                if (hashcsv.size())
                    m.hashes = split_hashes(hashcsv);
                files[path] = m;
            }
        }
    }
    // 2) replay WAL
    ifstream w(walFile);
    if (w)
    {
        string line;
        while (getline(w, line))
        {
            if (line.size() == 0)
                continue;
            if (line.rfind("PUT|", 0) == 0)
            {
                string rest = line.substr(4);
                // format same as snapshot
                vector<string> parts;
                size_t pos = 0;
                for (int k = 0; k < 5; k++)
                {
                    size_t p = rest.find('|', pos);
                    if (p == string::npos)
                    {
                        parts.push_back(rest.substr(pos));
                        pos = rest.size();
                        break;
                    }
                    parts.push_back(rest.substr(pos, p - pos));
                    pos = p + 1;
                }
                if (parts.size() < 5)
                    continue;
                string path = parts[0];
                FileMeta m{};
                m.version = stoull(parts[1]);
                m.size = stoull(parts[2]);
                m.mtime = stoull(parts[3]);
                m.chunk_sz = (size_t)stoul(parts[4]);
                string hashcsv = (pos < rest.size() ? rest.substr(pos) : string());
                if (hashcsv.size())
                    m.hashes = split_hashes(hashcsv);
                files[path] = m;
            }
            else if (line.rfind("DEL|", 0) == 0)
            {
                string path = line.substr(4);
                files.erase(path);
            }
        }
    }
    // done
}

// Append PUT record to WAL (durable)
void MetadataStore::append_put_wal(const string &path, const FileMeta &m)
{
    stringstream ss;
    ss << "PUT|" << path << "|" << m.version << "|" << m.size << "|" << m.mtime << "|" << m.chunk_sz << "|";
    for (size_t i = 0; i < m.hashes.size(); ++i)
    {
        ss << m.hashes[i];
        if (i + 1 < m.hashes.size())
            ss << ',';
    }
    ss << "\n";
    string rec = ss.str();

    lock_guard<mutex> lk(wal_mtx);
    int fd = open(walFile.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
    {
        perror("wal open");
        return;
    }
    ssize_t w = write(fd, rec.data(), rec.size());
    if (w != (ssize_t)rec.size())
    {
        // non-fatal for now
    }
    // fsync for durability (can be optimized for batching)
    fsync(fd);
    close(fd);
}

// Append DEL record to WAL
void MetadataStore::append_del_wal(const string &path)
{
    string rec = "DEL|" + path + "\n";
    lock_guard<mutex> lk(wal_mtx);
    int fd = open(walFile.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
    {
        perror("wal open");
        return;
    }
    write(fd, rec.data(), rec.size());
    fsync(fd);
    close(fd);
}

void MetadataStore::put(const string &path, const FileMeta &m)
{
    {
        lock_guard<mutex> lk(mu);
        files[path] = m;
    }
    append_put_wal(path, m);
    // maybe snapshot compact
    snapshot_if_needed();
}

bool MetadataStore::get(const string &path, FileMeta &out)
{
    lock_guard<mutex> lk(mu);
    auto it = files.find(path);
    if (it == files.end())
        return false;
    out = it->second;
    return true;
}

void MetadataStore::del(const string &path)
{
    {
        lock_guard<mutex> lk(mu);
        files.erase(path);
    }
    append_del_wal(path);
    snapshot_if_needed();
}

// snapshot if WAL grew beyond threshold
void MetadataStore::snapshot_if_needed()
{
    lock_guard<mutex> lk(wal_mtx);
    struct stat st;
    if (stat(walFile.c_str(), &st) == 0)
    {
        if ((size_t)st.st_size >= maxWalSize)
        {
            // create snapshot.tmp
            string tmp = snapshotFile + ".tmp";
            {
                lock_guard<mutex> lk2(mu);
                ofstream snap(tmp, ios::trunc);
                for (auto &kv : files)
                {
                    snap << "PUT|" << kv.first << "|" << kv.second.version << "|" << kv.second.size << "|" << kv.second.mtime << "|" << kv.second.chunk_sz << "|";
                    for (size_t i = 0; i < kv.second.hashes.size(); ++i)
                    {
                        snap << kv.second.hashes[i];
                        if (i + 1 < kv.second.hashes.size())
                            snap << ',';
                    }
                    snap << "\n";
                }
                snap.flush();
                // fsync omitted here for brevity
            }
            // atomic rename
            rename(tmp.c_str(), snapshotFile.c_str());
            // truncate WAL
            int fd = open(walFile.c_str(), O_TRUNC | O_WRONLY);
            if (fd >= 0)
                close(fd);
        }
    }
}

std::vector<std::pair<std::string, FileMeta>> MetadataStore::dumpAll()
{
    std::vector<std::pair<std::string, FileMeta>> out;
    std::lock_guard<std::mutex> lk(mu);
    out.reserve(files.size());
    for (auto &kv : files)
        out.emplace_back(kv.first, kv.second);
    return out;
}
