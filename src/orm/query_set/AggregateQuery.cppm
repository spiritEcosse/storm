module;

// Module global fragment - third-party C headers (macros not exported by modules)
#include <sqlite3.h>
#include <storm/macros.h>

// Define the module
export module storm.aggregate_query;

// Import required modules
import storm.selectable_query;
import storm.aggregate;
import storm.utils;
import storm.statement.select;

// Import standard header units
import <string>;
import <utility>;
import <vector>;
import <expected>;
import <type_traits>;
import <format>;
import <variant>;

export namespace storm {

    // Compile-time aggregate result type deduction
    template <AggregateKind K, typename FieldType> struct aggregate_result_type {
        using type = std::conditional_t<
                K == AggregateKind::Count,
                std::int64_t,
                std::conditional_t<
                        K == AggregateKind::Avg || K == AggregateKind::Sum,
                        double,
                        FieldType // Max/Min preserve field type
                        >>;
    };

    template <AggregateKind K, typename FieldType>
    using aggregate_result_t = typename aggregate_result_type<K, FieldType>::type;

    template <typename T>
    concept NumericType = std::is_arithmetic_v<T>;

    template <AggregateKind K, typename FieldType> struct aggregate_result {
        using type = std::conditional_t<
                K == AggregateKind::Count,
                std::int64_t,
                std::conditional_t<
                        (K == AggregateKind::Avg || K == AggregateKind::Sum),
                        std::conditional_t<NumericType<FieldType>, double, FieldType>,
                        FieldType>>;
    };

    // Simplified conversion with better compile-time optimization
    template <typename To, typename From>
    constexpr auto convert_value(const From& from) -> std::expected<To, std::string> {
        if constexpr (std::same_as<To, From>) {
            return from;
        } else if constexpr (std::convertible_to<From, To>) {
            return static_cast<To>(from);
        } else if constexpr (std::same_as<To, std::string>) {
            return std::format("{}", from);
        } else {
            return std::unexpected("Type conversion not supported");
        }
    }

    template <class T> class AggregateQuery : public SelectableQuery<T> {
      protected:
        std::vector<AggregateSpec> functionsSet;

      public:
        // Inherit constructors
        using SelectableQuery<T>::SelectableQuery;

        // Copy constructor
        AggregateQuery(const AggregateQuery& other)
            : SelectableQuery<T>(other)
            , functionsSet(other.functionsSet) {}

        // Move constructor
        AggregateQuery(AggregateQuery&& other) noexcept = default;

        // Copy assignment operator
        AggregateQuery& operator=(const AggregateQuery& other) {
            if (this != &other) {
                SelectableQuery<T>::operator=(other);
                functionsSet = other.functionsSet;
            }
            return *this;
        }

        // Move assignment operator
        AggregateQuery& operator=(AggregateQuery&& other) noexcept = default;

        // C++26 Aggregate functions with compile-time validation
        template <typename Self>
        constexpr auto&& max(this Self&& self, auto field, utils::fixed_string<32> alias = {}) noexcept {
            self.functionsSet.emplace_back(AggregateSpec::max(field, alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        template <typename Self>
        constexpr auto&& min(this Self&& self, auto field, utils::fixed_string<32> alias = {}) noexcept {
            self.functionsSet.emplace_back(AggregateSpec::min(field, alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        template <typename Self>
        constexpr auto&& avg(this Self&& self, auto field, utils::fixed_string<32> alias = {}) noexcept {
            self.functionsSet.emplace_back(AggregateSpec::avg(field, alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        template <typename Self>
        constexpr auto&& count(this Self&& self, auto field, utils::fixed_string<32> alias = {}) noexcept {
            self.functionsSet.emplace_back(AggregateSpec::count(field, alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        template <typename Self>
        constexpr auto&& sum(this Self&& self, auto field, utils::fixed_string<32> alias = {}) noexcept {
            self.functionsSet.emplace_back(AggregateSpec::sum(field, alias));
            self.query_flags.has_aggregates = true;
            return std::forward<Self>(self);
        }

        // Simplified GROUP_CONCAT API (reduced from 8 overloads to 4)

        // 1. Basic single field group_concat (field only)
        template <typename Self>
        auto&& group_concat(this Self&& self, auto field);

        // 2. Single field group_concat with alias (default separator)
        template <typename Self>
        auto&& group_concat(this Self&& self, auto field, utils::fixed_string<32> alias);

        // 3. Single field group_concat with alias and separator
        template <typename Self>
        auto&& group_concat(this Self&& self, auto field, utils::fixed_string<32> alias, utils::fixed_string<8> separator);

        // 4. Full-featured group_concat with all options
        template <typename Self>
        auto&& group_concat(
                this Self&& self,
                auto field,
                utils::fixed_string<32> alias,
                utils::fixed_string<8> separator,
                bool distinct
        );

        // C++26 Aggregate value methods with field() syntax - direct execution
        template <typename Self> [[nodiscard]] constexpr auto max_value(this Self&& self, auto field) noexcept {
            return self.template execute_aggregate<AggregateKind::Max>(field);
        }

        template <typename Self> [[nodiscard]] constexpr auto min_value(this Self&& self, auto field) noexcept {
            return self.template execute_aggregate<AggregateKind::Min>(field);
        }

        template <typename Self> [[nodiscard]] constexpr auto avg_value(this Self&& self, auto field) noexcept {
            return self.template execute_aggregate<AggregateKind::Avg>(field);
        }

        template <typename Self> [[nodiscard]] constexpr auto count_value(this Self&& self, auto field) noexcept {
            return self.template execute_aggregate<AggregateKind::Count>(field);
        }

        template <typename Self> [[nodiscard]] constexpr auto sum_value(this Self&& self, auto field) noexcept {
            return self.template execute_aggregate<AggregateKind::Sum>(field);
        }

        // FUNCTIONS API (declarations)
        template <typename Self, typename... Args> auto&& functions(this Self&& self, Args&&... args);

        // For custom SQL aggregates (GROUP_CONCAT, etc)
        template <typename ReturnType>
        [[nodiscard]] auto execute_custom_aggregate(std::string_view sql, std::string_view error_context)
                -> std::expected<ReturnType, std::string> {
            SelectOptions opts{
                    .functions_set = {AggregateSpec::custom_sql(sql)},
                    .where_clause  = this->_whereExpression,
            };

            auto result = SelectStatement<T>(this->conn, std::move(opts)).execute_values();
            if (!result) [[unlikely]]
                return std::unexpected(std::format("{}: {}", error_context, result.error()));

            if (result->empty() || result->front().empty()) [[unlikely]]
                return std::unexpected(std::format("{}: No results", error_context));

            return std::visit(
                    [](const auto& val) { return convert_value<ReturnType>(val); }, result->front().begin()->second
            );
        }

        // C++26 compile-time aggregate execution with field parameter
        template <AggregateKind Kind> [[nodiscard]] constexpr auto execute_aggregate(auto field) const {
            constexpr auto MemberPtr = decltype(field)::member_ptr;
            using FieldType =
                    typename refl::meta::member_pointer_traits<std::remove_const_t<decltype(MemberPtr)>>::member_type;
            using ResultType = aggregate_result_t<Kind, FieldType>;

            // C++26 compile-time spec generation with built-in validation
            AggregateSpec spec;
            if constexpr (Kind == AggregateKind::Max)
                spec = AggregateSpec::max(field);
            else if constexpr (Kind == AggregateKind::Min)
                spec = AggregateSpec::min(field);
            else if constexpr (Kind == AggregateKind::Avg)
                spec = AggregateSpec::avg(field);
            else if constexpr (Kind == AggregateKind::Count)
                spec = AggregateSpec::count(field);
            else
                spec = AggregateSpec::sum(field);

            SelectOptions opts{
                    .functions_set = {spec},
                    .where_clause  = this->_whereExpression,
            };

            auto result = SelectStatement<T>(this->conn, std::move(opts)).execute_values();
            if (!result) [[unlikely]]
                return std::expected<ResultType, std::string>(std::unexpected(result.error()));

            if (result->empty()) [[unlikely]]
                return std::expected<ResultType, std::string>(std::unexpected(std::string("No results")));

            // Use ranges for cleaner access
            auto& first_row = result->front();
            if (auto it = std::ranges::begin(first_row); it != std::ranges::end(first_row)) {
                return std::visit(
                        []<typename V>(const V& val) -> std::expected<ResultType, std::string> {
                            if constexpr (std::convertible_to<V, ResultType>) {
                                return static_cast<ResultType>(val);
                            } else {
                                return std::unexpected("Type conversion failed");
                            }
                        },
                        it->second
                );
            }

            return std::expected<ResultType, std::string>(std::unexpected(std::string("No data in result")));
        }

      protected:
        // Override SelectableQuery's build_select_options to include aggregate functions
        [[nodiscard]] SelectOptions build_select_options() const {
            return SelectOptions{
                .distinct_fields = this->distinctFields,
                .only_fields     = this->onlyFields,
                .functions_set   = functionsSet,
                .order_terms     = this->orderTerms,
                .group_by_fields = this->groupByFields,
                .limit           = this->_limit,
                .offset          = this->_offset,
                .where_clause    = this->_whereExpression,
            };
        }
    };

    // Functions method implementation
    template <typename T>
    template <typename Self, typename... Args>
    auto&& AggregateQuery<T>::functions(this Self&& self, Args&&... args) {
        // Reserve capacity
        self.functionsSet.reserve(self.functionsSet.size() + sizeof...(Args));

        // Process each function using fold expression
        (self.functionsSet.emplace_back(std::forward<Args>(args)), ...);
        return std::forward<Self>(self);
    }

    // Simplified GROUP_CONCAT implementations (reduced from 8 to 4 overloads)

    // 1. Basic single field group_concat (field only)
    template <typename T>
    template <typename Self>
    auto&& AggregateQuery<T>::group_concat(this Self&& self, auto field) {
        constexpr auto MemberPtr = decltype(field)::member_ptr;
        constexpr auto field_name = extract_field_name<MemberPtr>();
        constexpr auto table_name = extract_class_name<T>();

        std::string sql = "GROUP_CONCAT(";
        sql += std::string{table_name};
        sql += ".";
        sql += std::string{field_name};
        sql += " SEPARATOR ',')";
        sql += " AS \"group_concat_";
        sql += std::string{field_name};
        sql += "\"";

        self.functionsSet.emplace_back(AggregateSpec::custom_sql(sql));
        return std::forward<Self>(self);
    }

    // 2. Single field group_concat with alias (default separator)
    template <typename T>
    template <typename Self>
    auto&& AggregateQuery<T>::group_concat(this Self&& self, auto field, utils::fixed_string<32> alias) {
        constexpr auto MemberPtr = decltype(field)::member_ptr;
        constexpr auto field_name = extract_field_name<MemberPtr>();
        constexpr auto table_name = extract_class_name<T>();

        std::string sql = "GROUP_CONCAT(";
        sql += std::string{table_name};
        sql += ".";
        sql += std::string{field_name};
        sql += " SEPARATOR ',')";

        if (alias.c_str()[0] != '\0') {
            sql += " AS \"";
            sql += alias.c_str();
            sql += "\"";
        } else {
            sql += " AS \"group_concat_";
            sql += std::string{field_name};
            sql += "\"";
        }

        self.functionsSet.emplace_back(AggregateSpec::custom_sql(sql));
        return std::forward<Self>(self);
    }

    // 3. Single field group_concat with alias and separator
    template <typename T>
    template <typename Self>
    auto&& AggregateQuery<T>::group_concat(
            this Self&& self,
            auto field,
            utils::fixed_string<32> alias,
            utils::fixed_string<8> separator
    ) {
        constexpr auto MemberPtr = decltype(field)::member_ptr;
        constexpr auto field_name = extract_field_name<MemberPtr>();
        constexpr auto table_name = extract_class_name<T>();

        std::string sql = "GROUP_CONCAT(";
        sql += std::string{table_name};
        sql += ".";
        sql += std::string{field_name};
        sql += " SEPARATOR '";
        sql += separator.c_str();
        sql += "')";

        if (alias.c_str()[0] != '\0') {
            sql += " AS \"";
            sql += alias.c_str();
            sql += "\"";
        } else {
            sql += " AS \"group_concat_";
            sql += std::string{field_name};
            sql += "\"";
        }

        self.functionsSet.emplace_back(AggregateSpec::custom_sql(sql));
        return std::forward<Self>(self);
    }

    // 4. Full-featured group_concat with all options
    template <typename T>
    template <typename Self>
    auto&& AggregateQuery<T>::group_concat(
            this Self&& self,
            auto field,
            utils::fixed_string<32> alias,
            utils::fixed_string<8> separator,
            bool distinct
    ) {
        constexpr auto MemberPtr = decltype(field)::member_ptr;
        constexpr auto field_name = extract_field_name<MemberPtr>();
        constexpr auto table_name = extract_class_name<T>();

        std::string sql = "GROUP_CONCAT(";
        if (distinct) {
            sql += "DISTINCT ";
        }
        sql += std::string{table_name};
        sql += ".";
        sql += std::string{field_name};
        sql += " SEPARATOR '";
        sql += separator.c_str();
        sql += "')";

        if (alias.c_str()[0] != '\0') {
            sql += " AS \"";
            sql += alias.c_str();
            sql += "\"";
        } else {
            sql += " AS \"group_concat_";
            sql += std::string{field_name};
            sql += "\"";
        }

        self.functionsSet.emplace_back(AggregateSpec::custom_sql(sql));
        return std::forward<Self>(self);
    }

} // namespace storm