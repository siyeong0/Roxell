#include <IteratorTypes.h>
#include <TypeTraits.hpp>

#include <iterator>

using namespace rox::core;

static_assert(IsSame<ForwardIteratorTag, std::forward_iterator_tag>);
static_assert(IsBaseOf<ForwardIteratorTag, std::bidirectional_iterator_tag>);
