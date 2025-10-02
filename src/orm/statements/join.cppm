module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_join;

import storm_orm_statements_base;
import storm_orm_utilities;
import storm_db_concept;

import <expected>;
import <string>;
import <vector>;
import <utility>;
import <meta>;
import <type_traits>;
import <array>;

export namespace storm::orm::statements {

    using storm::orm::utilities::ConstexprString;

    // Compact JOIN statement - populates FK objects fully
    // Usage: message_qs.join<&Message::sender>().select()
    template <typename T, auto FKFieldPtr, storm::db::DatabaseConnection ConnType>
    class JoinStatement : private BaseStatement<T> {
        friend class BaseStatement<T>;
        using Base = BaseStatement<T>;
        using Connection = ConnType;
        using Error = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        // Extract FK type from member pointer
        using FKType = std::remove_cvref_t<decltype(std::declval<T>().*FKFieldPtr)>;
        using FKBase = BaseStatement<FKType>;

        // Get FK field name - consteval but called at runtime (works because inputs are constexpr)
        // For Message with single FK 'sender', this finds "sender"
        static consteval std::string_view get_fk_field_name() {
            // Find first FK field (works for Message with 1 FK)
            for (size_t i = 0; i < Base::field_count_; ++i) {
                if (Base::is_fk_field(Base::all_members_[i])) {
                    return std::meta::identifier_of(Base::all_members_[i]);
                }
            }
            return ""; // No FK field found
        }

        // Cached FK field name for runtime use
        static constexpr auto fk_field_name_ = get_fk_field_name();

        // Build qualified field list with table alias (e.g., "t1.id, t1.name")
        static std::string build_qualified_fields(std::string_view fields, std::string_view alias) {
            std::string result;
            size_t pos = 0;
            while (pos < fields.size()) {
                // Add table alias
                result += alias;
                result += '.';

                // Find next comma or end
                size_t comma = fields.find(',', pos);
                if (comma == std::string_view::npos) {
                    result += fields.substr(pos);
                    break;
                }

                result += fields.substr(pos, comma - pos);
                result += ", ";
                pos = comma + 2; // Skip ", "
            }
            return result;
        }

        // Build JOIN SQL at runtime to avoid compiler limitations
        static std::string build_join_sql() {
            std::string_view fk_name = fk_field_name_;

            std::string sql;
            sql.reserve(500); // Pre-allocate

            sql += "SELECT ";
            sql += build_qualified_fields(Base::field_names_, "t1");
            sql += ", ";
            sql += build_qualified_fields(FKBase::field_names_, "t2");
            sql += " FROM ";
            sql += Base::table_name_;
            sql += " t1 INNER JOIN ";
            sql += FKBase::table_name_;
            sql += " t2 ON t2.";
            sql += FKBase::pk_name_;
            sql += " = t1.";
            sql += fk_name;
            sql += "_id";

            return sql;
        }

        // Extract T fields from columns [0, Base::field_count_)
        template <size_t... Is>
        __attribute__((always_inline)) static void
        extract_t_fields(Statement* stmt, T& obj, std::index_sequence<Is...>) noexcept {
            ((extract_column_at<T, Is>(stmt, obj, Is)), ...);
        }

        // Extract FKType fields from columns [Base::field_count_, ...)
        template <size_t... Is>
        __attribute__((always_inline)) static void
        extract_fk_fields(Statement* stmt, FKType& fk_obj, std::index_sequence<Is...>) noexcept {
            ((extract_fk_column_at<FKType, Is>(stmt, fk_obj, Base::field_count_ + Is)), ...);
        }

        // Single column extraction for T (handles FK fields specially)
        template <typename ObjType, size_t MemberIdx>
        __attribute__((always_inline)) static void
        extract_column_at(Statement* stmt, ObjType& obj, int col_idx) noexcept {
            if constexpr (MemberIdx < Base::field_count_) {
                constexpr auto member = Base::all_members_[MemberIdx];

                // Skip FK field - it will be populated from joined table
                if constexpr (Base::is_fk_field(member)) {
                    // FK field is populated below, skip PK column
                    return;
                } else {
                    using FieldType = std::remove_cvref_t<decltype(obj.[:member:])>;

                    if constexpr (std::is_same_v<FieldType, int>) {
                        obj.[:member:] = stmt->extract_int(col_idx);
                    } else if constexpr (std::is_same_v<FieldType, int64_t>) {
                        obj.[:member:] = stmt->extract_int64(col_idx);
                    } else if constexpr (std::is_same_v<FieldType, std::string>) {
                        const unsigned char* text = stmt->extract_text_ptr(col_idx);
                        if (text) {
                            obj.[:member:] = std::string(reinterpret_cast<const char*>(text));
                        }
                    }
                    // Add more types as needed
                }
            }
        }

        // Column extraction for FK object (all fields)
        template <typename FKObjType, size_t MemberIdx>
        __attribute__((always_inline)) static void
        extract_fk_column_at(Statement* stmt, FKObjType& fk_obj, int col_idx) noexcept {
            if constexpr (MemberIdx < FKBase::field_count_) {
                constexpr auto member = FKBase::all_members_[MemberIdx];
                using FieldType = std::remove_cvref_t<decltype(fk_obj.[:member:])>;

                if constexpr (std::is_same_v<FieldType, int>) {
                    fk_obj.[:member:] = stmt->extract_int(col_idx);
                } else if constexpr (std::is_same_v<FieldType, int64_t>) {
                    fk_obj.[:member:] = stmt->extract_int64(col_idx);
                } else if constexpr (std::is_same_v<FieldType, std::string>) {
                    const unsigned char* text = stmt->extract_text_ptr(col_idx);
                    if (text) {
                        fk_obj.[:member:] = std::string(reinterpret_cast<const char*>(text));
                    }
                }
                // Add more types as needed
            }
        }

    public:
        explicit JoinStatement(Connection& conn) : conn_(conn) {}

        // Execute JOIN and return vector with fully populated FK objects
        [[nodiscard]] __attribute__((hot)) auto execute() noexcept
                -> std::expected<std::vector<T>, Error> {
            // Cache statement on first use
            if (!cached_join_stmt_) {
                std::string join_sql = build_join_sql();
                auto prepare_result = conn_.prepare_cached(join_sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_join_stmt_ = *prepare_result;
            }

            std::vector<T> results;
            results.resize(10000); // Pre-allocate

            int step_result;
            size_t row_count = 0;
            while ((step_result = cached_join_stmt_->step_raw()) == Statement::ROW_AVAILABLE
                   && row_count < results.size()) {
                T& obj = results[row_count];

                // Extract T fields (non-FK)
                extract_t_fields(cached_join_stmt_, obj, typename Base::field_indices_t{});

                // Extract FK object fully
                auto& fk_obj = obj.*FKFieldPtr;
                extract_fk_fields(cached_join_stmt_, fk_obj, std::make_index_sequence<FKBase::field_count_>{});

                row_count++;
            }

            // Handle overflow
            while (step_result == Statement::ROW_AVAILABLE) {
                results.emplace_back();
                T& obj = results.back();
                extract_t_fields(cached_join_stmt_, obj, typename Base::field_indices_t{});
                auto& fk_obj = obj.*FKFieldPtr;
                extract_fk_fields(cached_join_stmt_, fk_obj, std::make_index_sequence<FKBase::field_count_>{});
                row_count++;
                step_result = cached_join_stmt_->step_raw();
            }

            results.resize(row_count);

            if (step_result != Statement::NO_MORE_ROWS) {
                cached_join_stmt_->reset();
                return std::unexpected(Error{step_result, cached_join_stmt_->get_error_message()});
            }

            cached_join_stmt_->reset();
            return results;
        }

    private:
        Connection& conn_;
        mutable Statement* cached_join_stmt_ = nullptr;
    };

    // Phase 3: Multi-JOIN statement - populates multiple FK objects fully
    // Usage: message_qs.join<&Message::sender, &Message::receiver>().execute()
    template <typename T, storm::db::DatabaseConnection ConnType, auto... FKFieldPtrs>
        requires (sizeof...(FKFieldPtrs) > 1)
    class MultiJoinStatement : private BaseStatement<T> {
        friend class BaseStatement<T>;
        using Base = BaseStatement<T>;
        using Connection = ConnType;
        using Error = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        // Number of FK fields to join
        static constexpr size_t fk_count_ = sizeof...(FKFieldPtrs);

        // Extract FK type from member pointer at compile-time
        template <auto FKPtr>
        using FK_type = std::remove_cvref_t<decltype(std::declval<T>().*FKPtr)>;

        // Get BaseStatement type for FK at index
        template <size_t Idx, auto... Ptrs>
        struct FK_base_at_helper;

        // Base case: Idx is 0, return First
        template <auto First, auto... Rest>
        struct FK_base_at_helper<0, First, Rest...> {
            using type = BaseStatement<FK_type<First>>;
        };

        // Recursive case: Idx > 0, recurse with Rest
        template <size_t Idx, auto First, auto... Rest>
            requires (Idx > 0)
        struct FK_base_at_helper<Idx, First, Rest...> {
            using type = typename FK_base_at_helper<Idx - 1, Rest...>::type;
        };

        template <size_t Idx>
        using FKBase_at = typename FK_base_at_helper<Idx, FKFieldPtrs...>::type;

        // Calculate column offsets for each FK table
        template <size_t... Is>
        static constexpr auto calculate_column_offsets(std::index_sequence<Is...>) {
            std::array<size_t, fk_count_> offsets{};
            size_t current_offset = Base::field_count_;

            // Helper to get field count for FK at index I
            auto get_fk_field_count = []<size_t I>() constexpr {
                return FKBase_at<I>::field_count_;
            };

            // Calculate cumulative offsets
            ((offsets[Is] = current_offset,
              current_offset += get_fk_field_count.template operator()<Is>()), ...);

            return offsets;
        }

        static constexpr auto column_offsets_ =
            calculate_column_offsets(std::make_index_sequence<fk_count_>{});

        // Build FK field names recursively at compile time
        template <size_t MemberIdx, size_t FKIdx>
        static consteval void fill_fk_name(std::array<std::string_view, fk_count_>& names) {
            if constexpr (MemberIdx < Base::field_count_ && FKIdx < fk_count_) {
                constexpr auto member = Base::all_members_[MemberIdx];
                if constexpr (Base::is_fk_field(member)) {
                    names[FKIdx] = std::meta::identifier_of(member);
                    fill_fk_name<MemberIdx + 1, FKIdx + 1>(names);
                } else {
                    fill_fk_name<MemberIdx + 1, FKIdx>(names);
                }
            }
        }

        static consteval auto build_fk_field_names() {
            std::array<std::string_view, fk_count_> names{};
            fill_fk_name<0, 0>(names);
            return names;
        }

        static constexpr auto fk_field_names_ = build_fk_field_names();

        // Get FK field name at runtime
        static std::string_view get_fk_field_name_at(size_t idx) {
            return fk_field_names_[idx];
        }

        // Build qualified field list with table alias
        static std::string build_qualified_fields(std::string_view fields, std::string_view alias) {
            std::string result;
            size_t pos = 0;
            while (pos < fields.size()) {
                result += alias;
                result += '.';

                size_t comma = fields.find(',', pos);
                if (comma == std::string_view::npos) {
                    result += fields.substr(pos);
                    break;
                }

                result += fields.substr(pos, comma - pos);
                result += ", ";
                pos = comma + 2;
            }
            return result;
        }

        // Build multi-JOIN SQL
        template <size_t... Is>
        static std::string build_multi_join_sql_impl(std::index_sequence<Is...>) {
            std::string sql;
            sql.reserve(1000); // Pre-allocate for multiple joins

            // SELECT clause
            sql += "SELECT ";
            sql += build_qualified_fields(Base::field_names_, "t1");

            // Add qualified fields for each FK table
            ((sql += ", ",
              sql += build_qualified_fields(FKBase_at<Is>::field_names_,
                                           "t" + std::to_string(Is + 2))), ...);

            // FROM clause
            sql += " FROM ";
            sql += Base::table_name_;
            sql += " t1";

            // INNER JOIN clauses for each FK
            ((sql += " INNER JOIN ",
              sql += FKBase_at<Is>::table_name_,
              sql += " t",
              sql += std::to_string(Is + 2),
              sql += " ON t",
              sql += std::to_string(Is + 2),
              sql += ".",
              sql += FKBase_at<Is>::pk_name_,
              sql += " = t1.",
              sql += get_fk_field_name_at(Is),
              sql += "_id"), ...);

            return sql;
        }

        static std::string build_multi_join_sql() {
            return build_multi_join_sql_impl(std::make_index_sequence<fk_count_>{});
        }

        // Extract T fields from base table columns
        template <size_t... Is>
        __attribute__((always_inline)) static void
        extract_t_fields(Statement* stmt, T& obj, std::index_sequence<Is...>) noexcept {
            ((extract_column_at<Is>(stmt, obj, Is)), ...);
        }

        // Extract single column for T (skip FK fields)
        template <size_t MemberIdx>
        __attribute__((always_inline)) static void
        extract_column_at(Statement* stmt, T& obj, int col_idx) noexcept {
            if constexpr (MemberIdx < Base::field_count_) {
                constexpr auto member = Base::all_members_[MemberIdx];

                if constexpr (Base::is_fk_field(member)) {
                    return; // FK fields populated separately
                } else {
                    using FieldType = std::remove_cvref_t<decltype(obj.[:member:])>;

                    if constexpr (std::is_same_v<FieldType, int>) {
                        obj.[:member:] = stmt->extract_int(col_idx);
                    } else if constexpr (std::is_same_v<FieldType, int64_t>) {
                        obj.[:member:] = stmt->extract_int64(col_idx);
                    } else if constexpr (std::is_same_v<FieldType, std::string>) {
                        const unsigned char* text = stmt->extract_text_ptr(col_idx);
                        if (text) {
                            obj.[:member:] = std::string(reinterpret_cast<const char*>(text));
                        }
                    }
                }
            }
        }

        // Extract FK fields for FK at index FKIdx
        template <size_t FKIdx, auto FKPtr, size_t... FieldIs>
        __attribute__((always_inline)) static void
        extract_fk_fields_impl(Statement* stmt, FK_type<FKPtr>& fk_obj,
                               size_t col_offset, std::index_sequence<FieldIs...>) noexcept {
            using FKBase = FKBase_at<FKIdx>;
            ((extract_fk_field_at<FKBase, FieldIs>(stmt, fk_obj, col_offset + FieldIs)), ...);
        }

        // Extract single FK field
        template <typename FKBase, size_t FieldIdx>
        __attribute__((always_inline)) static void
        extract_fk_field_at(Statement* stmt, auto& fk_obj, int col_idx) noexcept {
            if constexpr (FieldIdx < FKBase::field_count_) {
                constexpr auto member = FKBase::all_members_[FieldIdx];
                using FieldType = std::remove_cvref_t<decltype(fk_obj.[:member:])>;

                if constexpr (std::is_same_v<FieldType, int>) {
                    fk_obj.[:member:] = stmt->extract_int(col_idx);
                } else if constexpr (std::is_same_v<FieldType, int64_t>) {
                    fk_obj.[:member:] = stmt->extract_int64(col_idx);
                } else if constexpr (std::is_same_v<FieldType, std::string>) {
                    const unsigned char* text = stmt->extract_text_ptr(col_idx);
                    if (text) {
                        fk_obj.[:member:] = std::string(reinterpret_cast<const char*>(text));
                    }
                }
            }
        }

        // Helper to get FK pointer at index
        template <size_t Idx, auto... Ptrs>
        struct get_ptr_at_helper;

        template <auto First, auto... Rest>
        struct get_ptr_at_helper<0, First, Rest...> {
            static constexpr auto value = First;
        };

        template <size_t Idx, auto First, auto... Rest>
            requires (Idx > 0)
        struct get_ptr_at_helper<Idx, First, Rest...> {
            static constexpr auto value = get_ptr_at_helper<Idx - 1, Rest...>::value;
        };

        template <size_t Idx>
        static constexpr auto get_fk_ptr_at = get_ptr_at_helper<Idx, FKFieldPtrs...>::value;

        // Extract FK at specific index
        template <size_t Idx>
        __attribute__((always_inline)) static void
        extract_fk_at(Statement* stmt, T& obj) noexcept {
            constexpr auto FKPtr = get_fk_ptr_at<Idx>;
            auto& fk_obj = obj.*FKPtr;
            extract_fk_fields_impl<Idx, FKPtr>(
                stmt, fk_obj, column_offsets_[Idx],
                std::make_index_sequence<FKBase_at<Idx>::field_count_>{}
            );
        }

        // Extract all FK objects using fold expression
        template <size_t... Is>
        __attribute__((always_inline)) static void
        extract_all_fks(Statement* stmt, T& obj, std::index_sequence<Is...>) noexcept {
            (extract_fk_at<Is>(stmt, obj), ...);
        }

    public:
        explicit MultiJoinStatement(Connection& conn) : conn_(conn) {}

        // Execute multi-JOIN and return vector with all FK objects fully populated
        [[nodiscard]] __attribute__((hot)) auto execute() noexcept
                -> std::expected<std::vector<T>, Error> {
            // Cache statement on first use
            if (!cached_join_stmt_) {
                std::string join_sql = build_multi_join_sql();
                auto prepare_result = conn_.prepare_cached(join_sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_join_stmt_ = *prepare_result;
            }

            std::vector<T> results;
            results.resize(10000); // Pre-allocate

            int step_result;
            size_t row_count = 0;
            while ((step_result = cached_join_stmt_->step_raw()) == Statement::ROW_AVAILABLE
                   && row_count < results.size()) {
                T& obj = results[row_count];

                // Extract base table fields (non-FK)
                extract_t_fields(cached_join_stmt_, obj, typename Base::field_indices_t{});

                // Extract all FK objects
                extract_all_fks(cached_join_stmt_, obj, std::make_index_sequence<fk_count_>{});

                row_count++;
            }

            // Handle overflow
            while (step_result == Statement::ROW_AVAILABLE) {
                results.emplace_back();
                T& obj = results.back();
                extract_t_fields(cached_join_stmt_, obj, typename Base::field_indices_t{});
                extract_all_fks(cached_join_stmt_, obj, std::make_index_sequence<fk_count_>{});
                row_count++;
                step_result = cached_join_stmt_->step_raw();
            }

            results.resize(row_count);

            if (step_result != Statement::NO_MORE_ROWS) {
                cached_join_stmt_->reset();
                return std::unexpected(Error{step_result, cached_join_stmt_->get_error_message()});
            }

            cached_join_stmt_->reset();
            return results;
        }

    private:
        Connection& conn_;
        mutable Statement* cached_join_stmt_ = nullptr;
    };

} // namespace storm::orm::statements
