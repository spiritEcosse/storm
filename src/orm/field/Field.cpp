module;

// (no legacy standard includes)

// Module implementation unit
module storm.field;

import <string>;
import <memory>;
import <type_traits>;
import <concepts>;

// No implementation needed here since all functions are templates or constexpr
// This file serves as a placeholder for future non-template implementations
// or explicit template instantiations

namespace storm {
    // If there are common types that you frequently use with Field<MemberPtr>,
    // you can add explicit instantiations here to reduce compile time
    
    // Example (uncomment and replace with actual types as needed):
    // template class Field<&User::id>;
    // template class Field<&User::name>;
    // template class Field<&Product::price>;
}
