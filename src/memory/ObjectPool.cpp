#include "memory/ObjectPool.h"

// Note: ObjectPool is a template class, so main implementation is in the header.
// This file exists to maintain consistent project structure and can contain
// explicit template instantiations if needed.

namespace lob::memory
{

    // TODO (Week 3-4): Add explicit template instantiations for commonly used types
    // This can improve compilation time and reduce binary size

    // Example explicit instantiations (uncomment when Order type is integrated):
    // #include "core/Order.h"
    // template class ObjectPool<core::Order, 1000000>;

} // namespace lob::memory
