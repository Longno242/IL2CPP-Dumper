#pragma once
#include <cctype>
#include <string>
#include <vector>

struct DumpConfig {
    std::string module_name = "GameAssembly.dll";
    std::string output_dir; // empty = Desktop\GameDump

    bool emit_cpp = true;
    bool emit_cs = true;
    bool emit_rs = true;
    bool emit_py = true;
    bool emit_json = true;
    bool emit_index = true;
    bool emit_images = true;
    bool emit_scripts = true;
    bool emit_stubs = true;
    bool emit_diff = true;
    bool emit_strings = true;
    bool emit_attrs = true;
    bool string_scan = true;
    size_t string_min_length = 4;
    size_t string_max_count = 50000;

    // Empty include list = include everything (then apply excludes).
    std::vector<std::string> include_images;
    std::vector<std::string> exclude_images = {
        "UnityEngine*", "Unity.*", "System*", "mscorlib*", "Mono.*", "netstandard*"
    };
    bool include_unity_engine = false;

    int init_retries = 120;
    int init_retry_ms = 500;
};

namespace dump_config {

inline bool WildcardMatch(const std::string& text, const std::string& pattern) {
    size_t t = 0, p = 0, star = std::string::npos, match = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++t; ++p;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            match = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++match;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

inline bool MatchesAny(const std::string& name, const std::vector<std::string>& patterns) {
    for (const auto& pat : patterns) {
        if (WildcardMatch(name, pat)) return true;
    }
    return false;
}

inline bool ImageAllowed(const DumpConfig& cfg, const std::string& image_name) {
    if (!cfg.include_images.empty() && !MatchesAny(image_name, cfg.include_images)) {
        return false;
    }
    if (!cfg.include_unity_engine) {
        if (MatchesAny(image_name, cfg.exclude_images)) {
            return false;
        }
    }
    return true;
}

} // namespace dump_config
