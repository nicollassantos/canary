/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "database/database.hpp"
#include "database/databasetasks.hpp"
#include "io/guild_repository_db.hpp"
#include "io/market_repository_db.hpp"
#include "lib/di/container.hpp"
#include "lib/metrics/metrics.hpp"
#include "lua/modules/modules.hpp"
#include "lua/scripts/scripts.hpp"

namespace {
	// Resolve against the production container, restoring any test container afterwards.
	class ProductionContainerScope {
	public:
		ProductionContainerScope() :
			previous(DI::getTestContainer()) {
			DI::setTestContainer(nullptr);
		}
		~ProductionContainerScope() {
			DI::setTestContainer(previous);
		}

		ProductionContainerScope(const ProductionContainerScope &) = delete;
		ProductionContainerScope &operator=(const ProductionContainerScope &) = delete;

	private:
		di::extension::injector<>* previous;
	};
}

TEST(ContainerSingletonsTest, ScriptsIsOwnedByTheContainer) {
	ProductionContainerScope scope;
	EXPECT_EQ(&g_scripts(), &DI::get<Scripts>());
	EXPECT_EQ(&g_scripts(), &g_scripts());
}

TEST(ContainerSingletonsTest, ModulesResolvesToOneInstance) {
	ProductionContainerScope scope;
	EXPECT_EQ(&g_modules(), &g_modules());
	EXPECT_EQ(&g_modules(), &DI::get<Modules>());
}

TEST(ContainerSingletonsTest, MetricsResolvesToOneInstance) {
	ProductionContainerScope scope;
	EXPECT_EQ(&g_metrics(), &g_metrics());
	EXPECT_EQ(&g_metrics(), &DI::get<metrics::Metrics>());
}

TEST(ContainerSingletonsTest, GuildRepositoryIsConstructedWithInjectedDatabase) {
	ProductionContainerScope scope;
	auto &repository = IGuildRepository::getInstance();
	EXPECT_NE(nullptr, dynamic_cast<GuildRepositoryDB*>(&repository));
	EXPECT_EQ(&repository, &IGuildRepository::getInstance());
}

// Market statistics are cached inside the adapter, so every caller must share one instance.
TEST(ContainerSingletonsTest, MarketRepositoryIsOneInjectedInstance) {
	ProductionContainerScope scope;
	auto &repository = IMarketRepository::getInstance();
	EXPECT_NE(nullptr, dynamic_cast<MarketRepositoryDB*>(&repository));
	EXPECT_EQ(&repository, &g_marketRepository());
}

// Repositories receive IDatabase by injection; it must be the Database that g_database() connects.
TEST(ContainerSingletonsTest, DatabasePortIsTheConnectedGlobalDatabase) {
	ProductionContainerScope scope;
	EXPECT_EQ(static_cast<IDatabase*>(&g_database()), &DI::get<IDatabase>());
	EXPECT_EQ(&DI::get<IDatabase>(), &IDatabase::getInstance());
}

// A test container that does not bind them must not get its own Database or DatabaseTasks:
// async queries would then run on a connection nobody connected.
TEST(ContainerSingletonsTest, DatabaseAndTasksIgnoreAnUnrelatedTestContainer) {
	di::extension::injector<> unrelated;
	auto* previous = DI::getTestContainer();
	DI::setTestContainer(&unrelated);
	const auto* databaseInTest = &g_database();
	const auto* tasksInTest = &g_databaseTasks();
	DI::setTestContainer(nullptr);
	const auto* database = &g_database();
	const auto* tasks = &g_databaseTasks();
	DI::setTestContainer(previous);

	EXPECT_EQ(database, databaseInTest);
	EXPECT_EQ(tasks, tasksInTest);
}

