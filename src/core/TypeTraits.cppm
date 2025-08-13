module;

// Module global fragment
// (no legacy standard includes)

// Define the module
export module storm.type_traits;

// Import standard header units
import <type_traits>;

export namespace storm {
    // Member pointer traits for extracting class and member types
    template<typename> struct member_pointer_traits;
    
    template<typename T, typename C>
    struct member_pointer_traits<T C::*> {
        using type = T;
        using class_type = C;
    };
}
