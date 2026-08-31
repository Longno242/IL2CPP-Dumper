#pragma once

#include <string>
#include <vector>

namespace module_discovery {

struct Candidate {
	std::string name;
	int score = 0;
	bool has_domain_get = false;
	bool has_il2cpp_exports = false;
	bool looks_renamed = false;
};

// Enumerate loaded modules and score IL2CPP candidates.
std::vector<Candidate> scan_loaded_modules();

// Pick the best candidate. Returns false if nothing plausible was found.
bool auto_detect(std::string* out_module_name);

} // namespace module_discovery
