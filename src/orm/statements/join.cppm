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

} // namespace storm::orm::statements
