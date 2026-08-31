#pragma once

#include <cstddef>

namespace export_scan {

void reset();
void build_cache(const char* module_name);
void* lookup(const char* api_name);
std::size_t export_count();
const char* last_path_label();

} // namespace export_scan
