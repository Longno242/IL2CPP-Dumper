#pragma once
#include "../dump_config.h"
#include "../dump_model.h"
#include <functional>
#include <string>

namespace static_dump {

bool Collect(const std::string& assembly_path,
             const std::string& metadata_path,
             const DumpConfig& cfg,
             DumpRoot& out,
             std::string& error_out,
             std::function<void(const std::string&)> log = nullptr);

} // namespace static_dump
