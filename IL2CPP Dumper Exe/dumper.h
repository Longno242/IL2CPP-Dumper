#pragma once
#include "dump_config.h"
#include "dump_model.h"
#include <functional>
#include <string>

// Static/offline EXE only — does not touch the runtime DLL / rrid path.
class GameDumper {
public:
    static bool DumpFromFiles(const std::string& assembly_path,
                              const std::string& metadata_path,
                              const std::string& output_dir,
                              const DumpConfig& cfg = DumpConfig{},
                              std::function<void(const std::string&)> logCallback = nullptr);

    static bool EmitAll(const DumpRoot& root,
                        const std::string& output_dir,
                        const DumpConfig& cfg,
                        std::function<void(const std::string&)> logCallback = nullptr);
};
