/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "io/highscore_repository.hpp"
#include "lib/di/container.hpp"

#include <optional>
#include <string>

/// Records the last highscore request and keeps its callback, so a test can deliver the rows
/// later, the way the dispatcher does in production.
class FakeHighscoreRepository final : public IHighscoreRepository {
public:
	struct Request {
		bool ourRank = false;
		std::string categoryName;
		uint32_t page = 0;
		uint8_t entriesPerPage = 0;
		uint32_t playerGUID = 0;
		uint32_t vocation = 0;
	};

	static di::extension::injector<> &install(di::extension::injector<> &injector) {
		injector.install(di::bind<IHighscoreRepository>.to<FakeHighscoreRepository>().in(di::singleton));
		return injector;
	}

	static FakeHighscoreRepository &installed() {
		return dynamic_cast<FakeHighscoreRepository &>(IHighscoreRepository::getInstance());
	}

	bool loadEntries(const std::string &categoryName, uint32_t page, uint8_t entriesPerPage, uint32_t vocation, OnLoaded onLoaded) override {
		lastRequest = Request { false, categoryName, page, entriesPerPage, 0, vocation };
		return accept(std::move(onLoaded));
	}

	bool loadOurRank(const std::string &categoryName, uint8_t entriesPerPage, uint32_t playerGUID, uint32_t vocation, OnLoaded onLoaded) override {
		lastRequest = Request { true, categoryName, 0, entriesPerPage, playerGUID, vocation };
		return accept(std::move(onLoaded));
	}

	/// Runs the stored callback, as the dispatcher would once the query finished.
	void deliver(std::optional<HighscorePage> page) {
		auto callback = std::move(pending);
		pending = nullptr;
		callback(std::move(page));
	}

	void reset() {
		lastRequest.reset();
		pending = nullptr;
		buildsQuery = true;
	}

	std::optional<Request> lastRequest;
	OnLoaded pending;
	/// false simulates a request no query can be built for.
	bool buildsQuery = true;

private:
	bool accept(OnLoaded onLoaded) {
		if (!buildsQuery) {
			return false;
		}
		pending = std::move(onLoaded);
		return true;
	}
};
