module;

#include <meta>

export module storm_meta;

export namespace storm::meta {
    // Field attribute enum for annotations
    enum class FieldAttr { primary, indexed, unique };

    // Check if member has primary attribute
    consteval bool has_primary_attr(std::meta::info member) {
        auto field_attr = std::meta::annotation_of_type<FieldAttr>(member);
        return field_attr.has_value() && field_attr.value() == FieldAttr::primary;
    }

    // Find primary key member with compile-time error if not found
    consteval std::meta::info find_primary_key(std::meta::info type) {
        for (std::meta::info member :
             std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())) {
            if (has_primary_attr(member)) {
                return member;
            }
        }

        // Compile-time error using throw in consteval context
        throw "Model must have exactly one field marked with [[=storm::meta::FieldAttr::primary]]";
    }
} // namespace storm::meta
