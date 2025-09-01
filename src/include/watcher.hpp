#pragma once
#include <string>
class MetadataStore; // fwd
void startWatcher(const std::string &dir, MetadataStore &store);
void startFileWatcher(const std::string &fullPath, MetadataStore &store);
