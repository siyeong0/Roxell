#include <FunctionTarget.hpp>

using namespace rox::core;

namespace
{
	struct Base
	{
	};
	struct Derived : Base
	{
	};
	struct ReturnsValue
	{
		int operator()(int) noexcept;
	};
	struct Throws
	{
		int operator()(int);
	};
	struct ReturnsLvalue
	{
		Derived& operator()() noexcept;
	};
	struct ReturnsRvalue
	{
		Derived&& operator()() noexcept;
	};
	struct ReturnsTemporary
	{
		Derived operator()() noexcept;
	};
	struct ReturnsConstLvalue
	{
		const Derived& operator()() noexcept;
	};

	static_assert(NothrowFunctionTarget<int, ReturnsValue, int>);
	static_assert(NothrowFunctionTarget<void, ReturnsValue, int>);
	static_assert(!NothrowFunctionTarget<int, Throws, int>);
	static_assert(!NothrowFunctionTarget<int, ReturnsValue>);
	static_assert(NothrowFunctionTarget<Base&, ReturnsLvalue>);
	static_assert(NothrowFunctionTarget<const Base&, ReturnsLvalue>);
	static_assert(!NothrowFunctionTarget<Base&, ReturnsTemporary>);
	static_assert(!NothrowFunctionTarget<Base&, ReturnsConstLvalue>);
	static_assert(!NothrowFunctionTarget<Base&, ReturnsRvalue>);
	static_assert(NothrowFunctionTarget<Base&&, ReturnsRvalue>);
	static_assert(!NothrowFunctionTarget<Base&&, ReturnsLvalue>);
}
