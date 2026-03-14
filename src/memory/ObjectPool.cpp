#include "memory/ObjectPool.h"

// Note: ObjectPool is a template class, so the main implementation is in the header.
// This file exists to maintain consistent project structure and can contain
// explicit template instantiations if needed.

namespace lob::memory
{

    // Explicit template instantiations for commonly used types can be added here
    // to improve compilation time and reduce binary size.
    //
    // Example:
    // #include "core/Order.h"
    // template class ObjectPool<core::Order, 1000000>;

} // namespace lob::memory
