#include "concurrency/RingBuffer.h"

// Note: RingBuffer is a template class, so main implementation is in the header.
// This file exists to maintain consistent project structure and can contain
// explicit template instantiations if needed.

namespace lob::concurrency
{

    // TODO (Week 5): Add explicit template instantiations for commonly used types

    // Example explicit instantiations (uncomment when Order type is integrated):
    // #include "core/Order.h"
    // template class RingBuffer<core::Order*, 65536>;  // Power of 2 for efficiency

} // namespace lob::concurrency
