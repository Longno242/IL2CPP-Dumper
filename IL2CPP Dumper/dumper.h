#pragma once
#include "rrid.hpp"
#include <string>
#include <functional>

class GameDumper {
public:
    // Writes every image / class / field / method into one .hpp file.
    // logCallback is optional; if set, every progress line is forwarded to it.
    static bool DumpAll(const std::string& output_file = "GameDump.hpp",
                        std::function<void(const std::string&)> logCallback = nullptr);
};
