#pragma once

#include <cstdlib>
#include <cstring>
#include <string>

namespace runtime_config {

inline std::string& module_name_override() {
	static std::string value;
	return value;
}

inline std::string& dump_output_dir() {
	static std::string dir;
	return dir;
}

inline int& init_retries() {
	static int retries = 120;
	return retries;
}

inline int& init_retry_ms() {
	static int delay_ms = 500;
	return delay_ms;
}

inline void apply() {
	if (const char* module = std::getenv("IL2CPP_MODULE")) {
		if (module[0] != '\0') {
			module_name_override() = module;
		}
	}

	if (const char* out = std::getenv("IL2CPP_DUMP_DIR")) {
		if (out[0] != '\0') {
			dump_output_dir() = out;
		}
	}

	if (const char* retries = std::getenv("IL2CPP_DUMP_RETRIES")) {
		const int value = std::atoi(retries);
		if (value > 0) {
			init_retries() = value;
		}
	}

	if (const char* delay = std::getenv("IL2CPP_DUMP_RETRY_MS")) {
		const int value = std::atoi(delay);
		if (value > 0) {
			init_retry_ms() = value;
		}
	}
}

} // namespace runtime_config
