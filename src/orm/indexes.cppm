module;

#include <meta>

export module storm_orm_indexes;

import <tuple>;
import <array>;

export namespace storm {

    // Composite index types — variadic on std::meta::info field reflections
    template <std::meta::info... Fields> struct Index {
        static constexpr auto fields = std::array{Fields...};
        static constexpr bool unique = false;
    };

    template <std::meta::info... Fields> struct UniqueIndex {
        static constexpr auto fields = std::array{Fields...};
        static constexpr bool unique = true;
    };

    // Default trait — no composite indexes. Users specialize for their models.
    template <typename T> struct Indexes {
        using type = std::tuple<>;
    };

    template <typename T> using indexes_t = typename Indexes<T>::type;

} // namespace storm
