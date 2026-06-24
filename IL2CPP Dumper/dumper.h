#pragma once
#include "rrid.hpp"
#include <string>
#include <functional>

class GameDumper {
public:
    // Writes every image / class / field / method into multiple format files
    // under output_dir (GameDump.hpp, GameDump.cs, GameDump.rs, GameDump.py, GameDump.json).
    static bool DumpAll(const std::string& output_dir,
                        std::function<void(const std::string&)> logCallback = nullptr);
};
