//
// Copyright (c) 2012-2020 Kris Jusiak (kris at jusiak dot net)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "lib/di/runtime_provider.hpp"

BOOST_DI_NAMESPACE_BEGIN
namespace extension {

	template <class...>
	class injector;

	template <class T, class... Ts>
	class injector<T, Ts...> : public di::injector<T, Ts...> {
	public:
		using di::injector<T, Ts...>::injector;
	};

	template <>
	class injector<> : public runtime_injector {
	public:
		using runtime_injector::injector;

		// True when T was installed explicitly (unbound abstract types assert on create).
		template <class T>
		[[nodiscard]] bool isBound() const {
			return this->cfg().bindings().contains(std::type_index(typeid(T)));
		}
	};

} // namespace extension
BOOST_DI_NAMESPACE_END
