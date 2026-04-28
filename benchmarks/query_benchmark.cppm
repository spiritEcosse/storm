// storm_benchmark_query
//
// SELECT-family benchmark fixture body. `QueryBenchmark<Model, test>` consumes
// a BenchmarkTest NTTP and builds the configured QuerySet + terminal once,
// then `run_terminal()` is called by the Google Benchmark loop body.
//
// Issue #235 — Phase 2.
//
// Was: benchmarks/operations/query_benchmark.hpp (textual header consumed by
// the now-deleted runner.hpp). The module conversion drops the raw-SQLite
// path (`execute_raw`, `bind_*`, `step_raw`) — Storm-only is the new contract.
// Anchor benchmarks for raw SQLite land in their own TU in Phase 4.

module;

// `sqlite3` typedef must be textually visible — `storm_benchmark_base` exports
// `get_db<Model>() -> sqlite3*` but the typedef is hidden behind its BMI.
#include <sqlite3.h>
#include <plf_hive/plf_hive.h>

// `<tuple>` is needed by models.hpp (Indexes<Person>::type) before
// reflection-annotated structs are visible. Annotations are blind across BMI
// boundaries (clang-p2996 #262, see feedback_cpp26_module_reflection_annotations),
// so model types must be textually visible in the consumer's GMF for any
// reflection-touching instantiation (e.g. QuerySet<User>::insert).
#include <tuple>

#include "models.hpp"

export module storm_benchmark_query;

import storm;
import storm_benchmark_base;
import storm_benchmark_registry;
import storm_benchmark_schema;

import <expected>;
import <format>;
import <iostream>;
import <meta>;
import <optional>;
import <ranges>;
import <string>;
import <string_view>;
import <tuple>;
import <utility>;
import <vector>;

export namespace storm::benchmark {

    using storm::orm::where::field;

    // ========================================================================
    // Field dispatcher — compile-time field lookup by name
    // ========================================================================
    template <typename Model> consteval auto dispatch_field(std::string_view field_name) -> std::meta::info {
        for (std::meta::info member :
             std::meta::nonstatic_data_members_of(^^Model, std::meta::access_context::unchecked())) {
            if (std::meta::identifier_of(member) == field_name) {
                return member;
            }
        }
        std::unreachable();
    }

    template <typename Model> consteval auto has_field(std::string_view field_name) -> bool {
        for (std::meta::info member :
             std::meta::nonstatic_data_members_of(^^Model, std::meta::access_context::unchecked())) {
            if (std::meta::identifier_of(member) == field_name) {
                return true;
            }
        }
        return false;
    }

    // ========================================================================
    // Operator/aggregate name validation concepts
    // ========================================================================
    template <auto op_str, typename FieldType>
    concept ValidOperator = []() consteval {
        constexpr std::string_view op = op_str.view();
        if (op == ">" || op == ">=" || op == "<" || op == "<=" || op == "==" || op == "!=" || op == "LIKE" ||
            op == "BETWEEN" || op == "IN") {
            return true;
        }
        if (op == "IS NULL" || op == "IS NOT NULL") {
            return orm::where::NullableField<FieldType>;
        }
        return false;
    }();

    template <auto func_str>
    concept ValidAggregate = []() consteval {
        constexpr std::string_view func = func_str.view();
        return func == "count" || func == "count_distinct" || func == "sum" || func == "avg" || func == "min" ||
               func == "max";
    }();

    // ========================================================================
    // QueryBenchmark — SELECT-family operations driven by a BenchmarkTest NTTP
    // ========================================================================
    template <typename Model, auto const& test>
    class QueryBenchmark : public DataBenchmarkBase<QueryBenchmark<Model, test>, Model, 1> {
        using Base = DataBenchmarkBase<QueryBenchmark<Model, test>, Model, 1>;

        static consteval auto is_setop() -> bool {
            return test.setop.enabled;
        }
        static consteval auto is_group_by_op() -> bool {
            return test.group_by.enabled;
        }
        static consteval auto is_aggregate_op() -> bool {
            return test.aggregate.enabled && !test.group_by.enabled;
        }
        static consteval auto is_distinct_op() -> bool {
            return test.distinct.enabled;
        }
        static consteval auto is_first_op() -> bool {
            constexpr auto op = test.operation.view();
            return op == "first" || op == "first_where";
        }
        static consteval auto is_get_op() -> bool {
            return test.operation.view() == "get_where";
        }

        // WHERE — pick the model the field actually lives on (FKRelated for
        // JOIN tests where the predicate field is on User, not FKMessage).
        static constexpr std::meta::info where_model_ = []() consteval {
            if constexpr (!test.where.enabled || !test.join.enabled) {
                return ^^Model;
            } else {
                return std::ranges::any_of(
                               std::views::counted(
                                       std::ranges::begin(test.where.conditions), test.where.condition_count
                               ),
                               [](const auto& condition) { return !has_field<Model>(condition.field.view()); }
                       )
                               ? ^^registry::FKRelated
                               : ^^Model;
            }
        }();

        using WhereModel = [:where_model_:];

        template <typename FieldType, TypedValue tv> static consteval auto typed_value_as() {
            if constexpr (std::is_same_v<FieldType, bool>) {
                return bool{tv.as_bool};
            } else if constexpr (std::is_floating_point_v<FieldType>) {
                return FieldType{tv.as_double};
            } else {
                return FieldType{tv.as_int};
            }
        }

        template <size_t I> static auto build_condition_expr() {
            constexpr auto& cond = test.where.conditions[I];
            static_assert(has_field<WhereModel>(cond.field.view()), "WHERE condition field not found on model");
            constexpr auto fi = dispatch_field<WhereModel>(cond.field.view());
            static_assert(std::meta::is_nonstatic_data_member(fi), "Field must be a non-static data member");
            constexpr std::string_view op = cond.op.view();
            using FieldType               = typename[:std::meta::type_of(fi):];

            if constexpr (op == "LIKE") {
                return build_compare_expr<fi, cond.op>(cond.values[0].as_string.view());
            } else if constexpr (op == "BETWEEN") {
                return build_compare_expr<fi, cond.op>(
                        typed_value_as<FieldType, cond.values[0]>(), typed_value_as<FieldType, cond.values[1]>()
                );
            } else if constexpr (op == "IN") {
                return []<size_t... Is>(std::index_sequence<Is...>) {
                    constexpr auto& c = test.where.conditions[I];
                    return build_compare_expr<fi, c.op>(typed_value_as<FieldType, c.values[Is]>()...);
                }(std::make_index_sequence<cond.value_count>{});
            } else if constexpr (op == "IS NULL" || op == "IS NOT NULL") {
                return build_compare_expr<fi, cond.op>();
            } else {
                return build_compare_expr<fi, cond.op>(typed_value_as<FieldType, cond.values[0]>());
            }
        }

        static constexpr auto apply_where(QuerySet<Model>& qs) -> void {
            if constexpr (test.where.enabled) {
                constexpr bool use_and = test.where.combine_and;

                [&]<size_t... Is>(std::index_sequence<Is...>) {
                    if constexpr (use_and) {
                        qs = qs.where((build_condition_expr<Is>() && ...));
                    } else {
                        qs = qs.where((build_condition_expr<Is>() || ...));
                    }
                }(std::make_index_sequence<test.where.condition_count>{});
            }
        }

        // Splice the operator method only after picking it — InExpression<bool>
        // etc. are not in ExpressionVariant, so eager instantiation of `in<bool>`
        // for a scalar-`==` test on a bool field would fail to compile.
        template <auto fi, auto op_str, typename... Ts>
            requires ValidOperator<op_str, typename std::remove_cvref_t<decltype(field<fi>())>::FieldType>
        static consteval auto resolve_operator() -> std::meta::info {
            using FE                      = std::remove_cvref_t<decltype(field<fi>())>;
            using FieldType               = typename FE::FieldType;
            constexpr std::string_view op = op_str.view();

            if constexpr (op == ">")
                return ^^FE::template operator><Ts...>;
            else if constexpr (op == ">=")
                return ^^FE::template operator>= <Ts...>;
            else if constexpr (op == "<")
                return ^^FE::template operator< <Ts...>;
            else if constexpr (op == "<=")
                return ^^FE::template operator<= <Ts...>;
            else if constexpr (op == "==")
                return ^^FE::template operator== <Ts...>;
            else if constexpr (op == "!=")
                return ^^FE::template operator!= <Ts...>;
            else if constexpr (op == "LIKE")
                return ^^FE::like;
            else if constexpr (op == "BETWEEN") {
                using V = std::tuple_element_t<0, std::tuple<Ts..., void>>;
                return ^^FE::template between<V>;
            } else if constexpr (op == "IN")
                return ^^FE::template in<Ts...>;
            else if constexpr (op == "IS NULL") {
                if constexpr (orm::where::NullableField<FieldType>)
                    return ^^FE::is_null;
            } else if constexpr (op == "IS NOT NULL") {
                if constexpr (orm::where::NullableField<FieldType>)
                    return ^^FE::is_not_null;
            }

            std::unreachable();
        }

        template <auto fi, auto op_str, typename... Ts>
            requires ValidOperator<op_str, typename std::remove_cvref_t<decltype(field<fi>())>::FieldType>
        static auto build_compare_expr(Ts&&... vs) {
            constexpr auto method = resolve_operator<fi, op_str, std::remove_cvref_t<Ts>...>();
            return (field<fi>().[:method:](std::forward<Ts>(vs)...));
        }

        // JOIN — schema-driven, normalizes single- and multi-FK paths
        static consteval auto join_fk_name(size_t i) -> std::string_view {
            if (test.join.fk_count >= 2)
                return test.join.fks[i].view();
            return test.join.fk.view();
        }

        template <auto... FKs> static consteval auto resolve_join() -> std::meta::info {
            constexpr std::string_view jt = test.join.type.view();
            if (jt == "left")
                return ^^QuerySet<Model>::template left_join<FKs...>;
            if (jt == "right")
                return ^^QuerySet<Model>::template right_join<FKs...>;
            return ^^QuerySet<Model>::template join<FKs...>;
        }

        template <auto... FKs> static auto dispatch_join_type(auto& qs) -> void {
            constexpr auto method = resolve_join<FKs...>();
            qs                    = qs.[:method:]();
        }

        static auto apply_join(auto& qs) -> void {
            if constexpr (test.join.enabled) {
                constexpr size_t N = test.join.fk_count >= 2 ? test.join.fk_count : size_t{1};
                [&]<size_t... Is>(std::index_sequence<Is...>) {
                    dispatch_join_type<registry::resolve_fk_ptr(join_fk_name(Is))...>(qs);
                }(std::make_index_sequence<N>{});
            }
        }

        // ORDER BY — chained recursively because order_by/limit/offset are
        // [[nodiscard]] and return QuerySet<..., true> by value (a fold-mutator
        // that takes `qs` by reference would silently drop clauses on each
        // type-shifted return). See project_queryset_finalization_type_shift.
        static consteval auto enabled_order_by_count() -> size_t {
            size_t n = 0;
            for (size_t i = 0; i < BenchmarkTest::MAX_ORDER_BY; ++i) {
                if (test.order_by[i].enabled)
                    ++n;
            }
            return n;
        }

        static auto apply_order_by(auto qs) {
            if constexpr (enabled_order_by_count() == 0) {
                return qs;
            } else {
                return apply_order_by_impl<0>(std::move(qs));
            }
        }

        template <size_t I> static auto apply_order_by_impl(auto qs) {
            if constexpr (I >= BenchmarkTest::MAX_ORDER_BY) {
                return qs;
            } else if constexpr (!test.order_by[I].enabled) {
                return apply_order_by_impl<I + 1>(std::move(qs));
            } else {
                constexpr auto& ob = test.order_by[I];
                static_assert(has_field<Model>(ob.field.view()), "ORDER BY field not found on model");
                constexpr auto oi  = dispatch_field<Model>(ob.field.view());
                constexpr bool asc = (ob.direction.view() != "DESC");
                if constexpr (ob.collate.view() != "") {
                    constexpr auto collation = parse_collate(ob.collate.view());
                    return apply_order_by_impl<I + 1>(qs.template order_by<oi, asc, collation>());
                } else {
                    return apply_order_by_impl<I + 1>(qs.template order_by<oi, asc>());
                }
            }
        }

        static auto apply_limit(auto qs) {
            if constexpr (!test.limit.enabled) {
                return qs;
            } else if constexpr (test.limit.value > 0 && test.limit.offset > 0) {
                return qs.limit(test.limit.value).offset(test.limit.offset);
            } else if constexpr (test.limit.value > 0) {
                return qs.limit(test.limit.value);
            } else if constexpr (test.limit.offset > 0) {
                return qs.offset(test.limit.offset);
            } else {
                return qs;
            }
        }

        static consteval auto parse_collate(std::string_view col_str) -> storm::orm::utilities::Collate {
            if (col_str == "BINARY")
                return storm::orm::utilities::Collate::Binary;
            if (col_str == "RTRIM")
                return storm::orm::utilities::Collate::RTrim;
            return storm::orm::utilities::Collate::NoCase;
        }

        static auto build_qs() {
            QuerySet<Model> qs;
            apply_join(qs);
            apply_where(qs);
            if constexpr (!is_setop()) {
                return apply_limit(apply_order_by(std::move(qs)));
            } else {
                return qs;
            }
        }

      public:
        explicit constexpr QueryBenchmark(int dataset_size = 1000) : Base(dataset_size) {}

        static auto create_model(int index = 0) -> Model {
            int i = index + 1;
            if constexpr (std::is_same_v<Model, Person>) {
                return Model{
                        .name      = std::format("Person{}", i),
                        .age       = 20 + (i % 50),
                        .salary    = 30000.0 + (i * 1000.0),
                        .is_active = (i % 2 == 0),
                        .score     = (i % 3 == 0) ? std::optional<int>(60 + (i % 40)) : std::nullopt
                };
            } else if constexpr (std::is_same_v<Model, User>) {
                return Model{.id = 0, .name = std::format("User{}", i), .age = 20 + (i % 50)};
            } else {
                return Model{};
            }
        }

        auto prepare(int iterations) -> void {
            if constexpr (test.join.enabled) {
                prepare_join_data();
            } else {
                Base::prepare_with_insert(iterations);
            }
            query_qs_ = build_qs();
            terminal_.emplace(build_terminal(query_qs_));
        }

      private:
        auto prepare_join_data() -> void {
            sqlite3* db = get_db<Model>();
            if (db == nullptr)
                return;

            int dataset_size = Base::batch_size();

            sqlite3_exec(db, "DELETE FROM FKMessage", nullptr, nullptr, nullptr);
            sqlite3_exec(db, "DELETE FROM User", nullptr, nullptr, nullptr);

            std::vector<registry::FKRelated> users;
            users.reserve(dataset_size);
            for (int i = 0; i < dataset_size; i++) {
                users.push_back(
                        registry::FKRelated{.id = 0, .name = std::format("User{}", i + 1), .age = 20 + (i % 50)}
                );
            }

            if (auto result = related_qs_.insert(users).execute(); !result.has_value()) {
                std::cerr << "Failed to insert users for benchmark\n";
                return;
            }

            auto user_select = related_qs_.select().execute();
            if (!user_select.has_value()) {
                std::cerr << "Failed to select users for benchmark\n";
                return;
            }

            std::vector<int64_t> user_ids;
            user_ids.reserve(user_select.value().size());
            for (const auto& user : user_select.value()) {
                user_ids.push_back(user.id);
            }

            std::vector<Model> messages;
            messages.reserve(dataset_size);
            for (int i = 0; i < dataset_size; i++) {
                registry::FKRelated sender{.id = static_cast<int>(user_ids[i % user_ids.size()]), .name = "", .age = 0};
                registry::FKRelated receiver{
                        .id = static_cast<int>(user_ids[(i + 1) % user_ids.size()]), .name = "", .age = 0
                };
                messages.push_back(
                        Model{.id = 0, .sender = sender, .receiver = receiver, .text = std::format("Message{}", i + 1)}
                );
            }

            auto msg_result = Base::qs().insert(messages).execute();
            if (!msg_result.has_value()) {
                std::cerr << "Failed to insert messages for benchmark\n";
            }
        }

      public:
        // Single-iteration runner — Google Benchmark's `for (auto _ : state)`
        // calls this once per iteration; data + terminal are built in prepare()
        // so the loop body stays tight.
        auto run_once() -> void {
            run_terminal(*terminal_);
        }

      private:
        template <typename Stmt, bool HasField = !test.aggregate.field.empty()>
            requires(!HasField) || ValidAggregate<test.aggregate.func>
        static consteval auto resolve_aggregate() -> std::meta::info {
            constexpr std::string_view func = test.aggregate.func.view();
            if constexpr (!HasField) {
                return ^^Stmt::template count<>;
            } else {
                static_assert(has_field<Model>(test.aggregate.field.view()), "aggregate field not found on model");
                constexpr auto fi = dispatch_field<Model>(test.aggregate.field.view());
                if (func == "count")
                    return ^^Stmt::template count<fi>;
                if (func == "count_distinct")
                    return ^^Stmt::template count_distinct<fi>;
                if (func == "sum")
                    return ^^Stmt::template sum<fi>;
                if (func == "avg")
                    return ^^Stmt::template avg<fi>;
                if (func == "min")
                    return ^^Stmt::template min<fi>;
                if (func == "max")
                    return ^^Stmt::template max<fi>;
                std::unreachable();
            }
        }

        template <typename Stmt> static auto apply_aggregate(Stmt& stmt) {
            constexpr auto method = resolve_aggregate<Stmt>();
            return stmt.[:method:]();
        }

        static auto build_distinct_stmt(auto& qs) {
            return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return qs.template distinct<dispatch_field<Model>(test.distinct.fields[Is].view())...>();
            }(std::make_index_sequence<test.distinct.field_count()>{});
        }

        static auto build_group_by_query(auto& qs) {
            auto gb = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return qs.template group_by<dispatch_field<Model>(test.group_by.fields[Is].view())...>();
            }(std::make_index_sequence<test.group_by.field_count()>{});

            if constexpr (test.group_by.having.enabled) {
                static_assert(has_field<Model>(test.group_by.having.field.view()), "HAVING field not found on model");
                constexpr auto hf = dispatch_field<Model>(test.group_by.having.field.view());
                gb                = gb.having(
                        build_compare_expr<hf, test.group_by.having.op>(
                                auto{numeric_value<test.group_by.having.value>()}
                        )
                );
            }

            return apply_aggregate(gb);
        }

        static consteval auto resolve_setop() -> std::meta::info {
            constexpr std::string_view type = test.setop.type.view();
            if (type == "union")
                return ^^QuerySet<Model>::template union_<>;
            if (type == "union_all")
                return ^^QuerySet<Model>::template union_all<>;
            if (type == "except")
                return ^^QuerySet<Model>::template except_<>;
            return ^^QuerySet<Model>::template intersect_<>;
        }

        struct SetOpThresholds {
            int  left;
            int  right;
            bool overlap;
        };
        static consteval auto setop_thresholds() -> SetOpThresholds {
            constexpr std::string_view type    = test.setop.type.view();
            constexpr bool             overlap = (type == "except" || type == "intersect");
            return {overlap ? 55 : 45, overlap ? 35 : 45, overlap};
        }

        static auto build_setop(auto& qs) {
            constexpr auto th = setop_thresholds();

            auto qs_left = qs.where(field<^^Model::age>() < th.left);

            QuerySet<Model> qs_right_base;
            auto            qs_right = th.overlap ? qs_right_base.where(field<^^Model::age>() > th.right)
                                                  : qs_right_base.where(field<^^Model::age>() >= th.right);

            constexpr auto method  = resolve_setop();
            auto           builder = qs_left.[:method:](qs_right);
            builder                = apply_order_by(std::move(builder));
            builder                = apply_limit(std::move(builder));
            return builder;
        }

        static auto build_terminal(auto& qs) {
            if constexpr (is_setop()) {
                return build_setop(qs);
            } else if constexpr (is_aggregate_op()) {
                return apply_aggregate(qs);
            } else if constexpr (is_group_by_op()) {
                return build_group_by_query(qs);
            } else if constexpr (is_distinct_op()) {
                return build_distinct_stmt(qs);
            } else if constexpr (is_first_op()) {
                return qs.first();
            } else if constexpr (is_get_op()) {
                return qs.get();
            } else {
                return qs.select();
            }
        }

        static auto run_terminal(auto& stmt) -> void {
            (void)stmt.execute();
        }

        using QsType       = decltype(build_qs());
        using TerminalType = decltype(build_terminal(std::declval<QsType&>()));

        QsType                        query_qs_;
        std::optional<TerminalType>   terminal_;
        QuerySet<registry::FKRelated> related_qs_;
    };

} // namespace storm::benchmark
