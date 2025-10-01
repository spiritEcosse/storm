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

        // Simplified: Get FK field info from template parameter
        // FKFieldPtr is &Message::sender, we extract field name via reflection on T
        static consteval std::string_view get_fk_field_name() {
            // Find first FK field - simplified for now
            // TODO: Match exact field from FKFieldPtr
            for (size_t i = 0; i < Base::field_count_; ++i) {
                if (Base::is_fk_field(Base::all_members_[i])) {
                    return std::meta::identifier_of(Base::all_members_[i]);
                }
            }
            throw "No FK field found";
        }

        static constexpr auto fk_field_name_ = get_fk_field_name();

        // Calculate JOIN SQL size
        static consteval size_t calculate_join_sql_size() {
            size_t size = 0;
            size += 7;  // "SELECT "
            size += Base::calculate_field_names_size();
            size += 2;  // ", "
            size += FKBase::calculate_field_names_size();
            size += 6;  // " FROM "
            size += Base::table_name_.size();
            size += 13; // " t1 INNER JOIN "
            size += FKBase::table_name_.size();
            size += 8;  // " t2 ON t2."
            size += FKBase::pk_name_.size();
            size += 8;  // " = t1."
            size += fk_field_name_.size();
            size += 3;  // "_id"
            return size + 50; // Buffer
        }

        // Build JOIN SQL at compile-time
        static consteval auto build_join_sql_array() {
            constexpr size_t sql_size = calculate_join_sql_size();
            ConstexprString<sql_size> result;

            result.append("SELECT ");
            // Append all fields from T1
            result.append(Base::build_all_field_names_list());
            result.append(", ");
            // Append all fields from T2
            result.append(FKBase::build_all_field_names_list());
            result.append(" FROM ");
            result.append(Base::table_name_);
            result.append(" t1 INNER JOIN ");
            result.append(FKBase::table_name_);
            result.append(" t2 ON t2.");
            result.append(FKBase::pk_name_);
            result.append(" = t1.");
            result.append(fk_field_name_);
            result.append("_id");

            return result;
        }

        static constexpr auto join_sql_array = build_join_sql_array();
        static inline const std::string join_sql_string = std::string(join_sql_array);

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
                auto prepare_result = conn_.prepare_cached(join_sql_string);
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
                extract_fk_fields(cached_join_stmt_, fk_obj, typename FKBase::field_indices_t{});

                row_count++;
            }

            // Handle overflow
            while (step_result == Statement::ROW_AVAILABLE) {
                results.emplace_back();
                T& obj = results.back();
                extract_t_fields(cached_join_stmt_, obj, typename Base::field_indices_t{});
                auto& fk_obj = obj.*FKFieldPtr;
                extract_fk_fields(cached_join_stmt_, fk_obj, typename FKBase::field_indices_t{});
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
