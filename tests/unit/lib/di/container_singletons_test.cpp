/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

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
