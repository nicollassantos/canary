/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "database/query_recorder.hpp"

namespace query_recorder {

	namespace {
		thread_local std::vector<std::string>* active = nullptr;
	} // namespace

	Scope::Scope(std::vector<std::string> &statements) :
		previous(active) {
		active = &statements;
	}

	Scope::~Scope() {
		active = previous;
	}

	bool record(std::string_view statement) {
		if (active == nullptr) {
			return false;
		}
		active->emplace_back(statement);
		return true;
	}

} // namespace query_recorder
