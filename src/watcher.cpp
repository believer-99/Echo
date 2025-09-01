// watcher.cpp
#include "watcher.h"
#include "metadata.h"
#include "transfer.hpp" // for announceChange
#include <sys/inotify.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <openssl/sha.h>

using namespace std;
using clockt = chrono::steady_clock;

static unordered_map<string, clockt::time_point> last_evt;
static mutex mtx;
static condition_variable cv;
static bool watcher_running = true;
static string base_dir;

static void process_stable_file(const string &relpath, MetadataStore &store)
{
    // compute chunk hashes and meta (simple fixed chunks)
    string full = base_dir + "/" + relpath;
    struct stat st;
    if (stat(full.c_str(), &st) != 0)
        return;
    FileMeta m;
    m.size = (size_t)st.st_size;
    m.mtime = (uint64_t)st.st_mtime;
    m.chunk_sz = DEFAULT_CHUNK_SZ;

    // open and compute SHA256 per chunk
    FILE *f = fopen(full.c_str(), "rb");
    if (!f)
        return;
    vector<uint8_t> buf(m.chunk_sz);
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
        m.hashes.push_back(string(hex, 64));
        if (r < m.chunk_sz)
            break;
    }
    fclose(f);

    // version bump - read current version if any
    FileMeta old;
    if (store.get(relpath, old))
        m.version = old.version + 1;
    else
        m.version = 1;

    // store to metadata (which appends WAL)
    store.put(relpath, m);

    // let transfer layer announce to connected peers
    Transfer::announceChange(relpath, m);
}

// debounce thread: wakes periodically and processes files whose last_evt older than db_ms
static void debounce_thread_fn(MetadataStore &store, int db_ms)
{
    while (watcher_running)
    {
        vector<string> to_process;
        {
            unique_lock<mutex> lk(mtx);
            if (last_evt.empty())
                cv.wait_for(lk, chrono::milliseconds(100));
            auto now = clockt::now();
            for (auto it = last_evt.begin(); it != last_evt.end();)
            {
                auto due = it->second + chrono::milliseconds(db_ms);
                if (due <= now)
                {
                    to_process.push_back(it->first);
                    it = last_evt.erase(it);
                }
                else
                    ++it;
            }
        }
        for (auto &r : to_process)
            process_stable_file(r, store);
        this_thread::sleep_for(chrono::milliseconds(10));
    }
}

// start watcher on directory (non-recursive). For recursion you need to add watches for subdirs.
void startWatcher(const string &dir, MetadataStore &store)
{
    base_dir = dir;
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0)
    {
        perror("inotify_init");
        return;
    }

    int wd = inotify_add_watch(fd, dir.c_str(), IN_MODIFY | IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE | IN_DELETE);
    if (wd < 0)
    {
        perror("inotify_add_watch");
        close(fd);
        return;
    }

    // start debounce worker
    thread(debounce_thread_fn, std::ref(store), 100).detach();

    // event loop
    const size_t BUF_SZ = 4096;
    vector<char> buf(BUF_SZ);
    while (watcher_running)
    {
        ssize_t len = read(fd, buf.data(), BUF_SZ);
        if (len <= 0)
        {
            this_thread::sleep_for(chrono::milliseconds(30));
            continue;
        }
        size_t idx = 0;
        while (idx < (size_t)len)
        {
            inotify_event *ev = (inotify_event *)(buf.data() + idx);
            if (ev->len)
            {
                string filename = ev->name;
                // ignore directories
                if (ev->mask & IN_ISDIR)
                { /* if dir created, optionally add watch */
                }
                else
                {
                    // register event time (debounce)
                    {
                        lock_guard<mutex> lk(mtx);
                        last_evt[filename] = clockt::now();
                    }
                    cv.notify_one();
                }
            }
            idx += sizeof(inotify_event) + ev->len;
        }
    }
    inotify_rm_watch(fd, wd);
    close(fd);
}
