#pragma once

#include <functional>
#if defined(_MSC_VER)
#include <xmemory>
#endif

namespace rox::core
{
#if defined(_MSC_VER)
    inline constexpr std::size_t NativeContainerProxyBytes = sizeof(std::_Container_proxy);

    template <typename Callable> inline constexpr bool NativeFunctionStoresInline =
        !std::_Is_large<std::_Func_impl_no_alloc<Callable, void>>;
#endif
}
