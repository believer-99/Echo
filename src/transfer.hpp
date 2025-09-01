#pragma once
#include <cstdint>
#include <string>
#include <vector>

class MetadataStore;
struct FileMeta;

enum MessageType : uint8_t
{
    MT_FILE_DESC = 1,    // writer announces file + chunk hashes
    MT_GET_CHUNKS = 2,   // reader requests missing chunk indices
    MT_PUT_CHUNK = 3,    // writer sends chunk data
    MT_OPEN_NOTEPAD = 4, // writer announces notepad file to open
};

namespace Transfer
{
    // Send a FILE_DESC announcement to a socket
    bool announceChangeToSocket(int sockfd, const std::string &path, const FileMeta &m);
    // Broadcast to all connected peers (sockets maintained in Connection module)
    void announceChange(const std::string &path, const FileMeta &m);
    // Broadcast an OPEN_NOTEPAD event so readers auto-launch a viewer
    void announceOpenNotepad(const std::string &path);
    // Process an incoming frame on the reader side
    void processIncomingFrame(int sockfd, uint8_t type, const std::vector<uint8_t> &payload,
                              MetadataStore &store, const std::string &basedir);
}
