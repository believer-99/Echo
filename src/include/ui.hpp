#pragma once
#include <string>

// Launch a simple Qt notepad editor (writer). Blocks until exit.
void ui_start_writer(const std::string &relpath);

// Launch a simple Qt notepad viewer (reader). Blocks until window closed.
void ui_start_reader(const std::string &relpath);
