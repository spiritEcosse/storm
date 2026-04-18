#pragma once

/**
 * QueryBenchmark<Model, test> — unified SELECT-family benchmark driven by
 * the nested BenchmarkTest schema.
 *
 * Replaces ALL per-operation benchmark classes (SelectBenchmark, AggregateBenchmark,
 * DistinctBenchmark, WhereOperator benchmarks, FirstBenchmark, GetBenchmark,
 * SetOpBenchmark, GroupBy benchmarks) with a single class whose configuration
 * comes entirely from the `test` NTTP (a reference into BENCHMARK_TESTS).
 *
 * All feature dispatch is compile-time via `if constexpr` on test.where.enabled,
 * test.join.enabled, test.group_by.enabled, test.aggregate.enabled, etc.
 *
 * INSERT/UPDATE/DELETE operations stay with their existing classes in base.hpp.
 */

#include "base.hpp"
#include "../model_registry.hpp"
#include "../schema.hpp"
#include <format>
#include <meta>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace storm::benchmark {

    using storm::orm::where::field;

    // ========================================================================
    // Field dispatcher — compile-time field lookup by name (standalone version)
    // ========================================================================
    // Field lookup by name. Callers must gate with static_assert(has_field<Model>(name))
    // so a missing field fails at the call site with a clear message. If the static_assert
    // is in place, the loop is guaranteed to return; std::unreachable() satisfies the
    // return type (field_name is a runtime string_view, so we cannot constrain with requires).
    template <typename Model> consteval auto dispatch_field(std::string_view field_name) -> std::meta::info {
        for (std::meta::info member :
             std::meta::nonstatic_data_members_of(^^Model, std::meta::access_context::unchecked())) {
            if (std::meta::identifier_of(member) == field_name) {
                return member;
            }
        }
        std::unreachable();
    }

    // Check whether a field name exists on a model at compile time
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
    // Concepts that validate op-strings and aggregate function names at the
    // call site. A bogus value produces a constraint-violation error naming
    // the concept, rather than a late `throw "…"` in a consteval body.
    // ========================================================================
    template <auto op_str, typename FieldType>
    concept ValidOperator = []() consteval {
        constexpr std::string_view op = op_str.view();
        if (op == ">" || op == ">=" || op == "<" || op == "<=" || op == "==" || op == "!=" || op == "LIKE" ||
            op == "BETWEEN" || op == "IN") {
            return true;
        }
        // IS NULL / IS NOT NULL require an optional field.
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
    // QueryBenchmark — one class for all SELECT-family operations
    // ========================================================================
    template <typename Model, auto const& test>
    class QueryBenchmark : public DataBenchmarkBase<QueryBenchmark<Model, test>, Model, 1> {
        using Base = DataBenchmarkBase<QueryBenchmark<Model, test>, Model, 1>;

        // ====================================================================
        // Operation family classification helpers (compile-time)
        // ====================================================================
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
        // Exact-match against the operation string. Enumerated from
        // benchmarks/tests/benchmark_tests.yaml — "first", "first_where", "get_where".
        // Prefix matching would silently mis-classify future values like
        // "first_or_default" or "get_one".
        static consteval auto is_first_op() -> bool {
            constexpr auto op = test.operation.view();
            return op == "first" || op == "first_where";
        }
        static consteval auto is_get_op() -> bool {
            return test.operation.view() == "get_where";
        }

        // ====================================================================
        // WHERE clause — handles all operator types
        // Modifies qs in-place (where/join/order_by/limit return auto&& to self).
        // ====================================================================

        // Resolve WHERE field model: use FKRelated for JOIN tests when
        // the field isn't on the base Model (e.g., age on User via FKMessage JOIN).
        // Checks all enabled conditions.
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

        // Extract a value from TypedValue as the field's native type.
        // Bool fields must read `as_bool` (not `as_int`) to avoid narrowing
        // in brace-init inside Field::operator==/in/between/compare.
        template <typename FieldType, TypedValue tv> static consteval auto typed_value_as() {
            if constexpr (std::is_same_v<FieldType, bool>) {
                return bool{tv.as_bool};
            } else if constexpr (std::is_floating_point_v<FieldType>) {
                return FieldType{tv.as_double};
            } else {
                return FieldType{tv.as_int};
            }
        }

        // Build a single comparison expression for condition at index I.
        // Branches only on argument shape — op-string → method dispatch lives in resolve_operator.
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

        // Build a single comparison expression via ^^ reflection splice.
        // op_str is ConstexprString (structural type, valid as NTTP).
        //
        // Each ^^FE::template <op><Ts...> forces the referenced function
        // template to be fully instantiated (body too). Splice only the op
        // actually selected — otherwise e.g. `in<bool>` instantiates even for
        // scalar `==` on a bool field, hitting `InExpression<bool>` which is
        // not in ExpressionVariant.
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
                // between has 1 template param (V used for both min and max).
                // All Ts are the same type here — collapse to the first.
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

        // ====================================================================
        // JOIN step — schema-driven using model_registry
        // Normalizes single-FK (`join.fk`) and multi-FK (`join.fks[]`) paths
        // and folds both into one pack expansion.
        // ====================================================================
        static consteval auto join_fk_name(size_t i) -> std::string_view {
            if (test.join.fk_count >= 2)
                return test.join.fks[i].view();
            return test.join.fk.view();
        }

        // Join type dispatch via ^^ reflection splice — join methods are now
        // regular (non-deducing-this) templates and reflectable.
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

        // ====================================================================
        // ORDER BY — fold over all slots; each slot's `enabled` guards it
        // ====================================================================
        // QuerySet::order_by/limit/offset are [[nodiscard]] and return
        // QuerySet<..., true> (finalized type) BY VALUE — a reference-taking helper
        // that discards the result silently drops the clauses. Helpers here take qs
        // by value and return the updated qs; callers reassign.
        //
        // Because a mid-fold reassignment would shift qs's type between slots
        // (QuerySet<..., false> → QuerySet<..., true>), all enabled order_by slots
        // are collected into one template pack and passed to order_by<...>() in a
        // single call. QuerySet's order_by<auto... Args>() accepts a flat pack of
        // alternating field / asc / [collate] NTTPs via make_order_by_wrapper.

        // Count enabled order_by slots at compile time
        static consteval auto enabled_order_by_count() -> size_t {
            size_t n = 0;
            for (size_t i = 0; i < BenchmarkTest::MAX_ORDER_BY; ++i) {
                if (test.order_by[i].enabled)
                    ++n;
            }
            return n;
        }

        // Apply all enabled order_by slots in one call (single type shift).
        static auto apply_order_by(auto qs) {
            if constexpr (enabled_order_by_count() == 0) {
                return qs;
            } else {
                // Delegate per MAX_ORDER_BY; each slot either dispatches with its
                // args or skips. Helper below chains enabled slots sequentially.
                return apply_order_by_impl<0>(std::move(qs));
            }
        }

        // Recursive chain: at each slot I, if enabled, call qs.order_by<args>()
        // (which finalizes qs's type) then recurse with updated qs; else skip.
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

        // ====================================================================
        // LIMIT / OFFSET
        // ====================================================================
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

        // ====================================================================
        // Collate helper
        // ====================================================================
        static consteval auto parse_collate(std::string_view col_str) -> storm::orm::utilities::Collate {
            if (col_str == "BINARY")
                return storm::orm::utilities::Collate::Binary;
            if (col_str == "RTRIM")
                return storm::orm::utilities::Collate::RTrim;
            return storm::orm::utilities::Collate::NoCase;
        }

        // ====================================================================
        // Fully configured QuerySet — JOIN → WHERE → ORDER BY → LIMIT/OFFSET
        // ====================================================================
        static auto build_qs() {
            QuerySet<Model> qs;
            apply_join(qs);
            apply_where(qs);
            // For setops, order_by/limit apply to the outer union result,
            // not the inner SELECTs — build_setop applies them on the builder.
            // order_by/limit/offset transition qs to the finalized type <..., true>,
            // so the return type here depends on is_setop().
            if constexpr (!is_setop()) {
                return apply_limit(apply_order_by(std::move(qs)));
            } else {
                return qs;
            }
        }

      public:
        explicit constexpr QueryBenchmark(int dataset_size = 1000) : Base(dataset_size) {}

        // ====================================================================
        // create_model — varied row generation per model type
        // ====================================================================
        // TODO: i we already have this in base.hpp - DRY
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
                // FKMessage — not used directly (prepare_join_data creates these)
                return Model{};
            }
        }

        // ====================================================================
        // prepare — data setup + build terminal once
        // ====================================================================
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
        // TODO: looks big one, lets separate by funcs or reduce reusing
        // create model we have if its possible
        auto prepare_join_data() -> void {
            sqlite3* db = get_db<Model>();
            if (db == nullptr)
                return;

            int dataset_size = Base::batch_size();

            // Clear tables
            sqlite3_exec(db, "DELETE FROM FKMessage", nullptr, nullptr, nullptr);
            sqlite3_exec(db, "DELETE FROM User", nullptr, nullptr, nullptr);

            // Insert related records (users)
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

            // SELECT back to get auto-generated user IDs
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

            // Insert base records (FKMessages) with FK references
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
        // ====================================================================
        // print_info — operation-aware display
        // ====================================================================
        auto print_info() const -> void {
            std::cout << std::format("Operation: {}\n  Dataset: {} rows\n", test.operation.view(), Base::batch_size());
        }

        // ====================================================================
        // execute — Storm ORM path, reuses stored terminal
        // ====================================================================
        auto execute(int iterations) -> void {
            for (int i = 0; i < iterations; i++) {
                run_terminal(*terminal_);
            }
        }

        // ====================================================================
        // execute_raw — Raw SQLite path, SQL from stored terminal
        // ====================================================================
        // TODO: I guess we can use repeated parts from CRUD - DRY
        // maybe separate by funcs
        auto execute_raw(int iterations) -> void {
            sqlite3* db = get_db<Model>();
            if (db == nullptr) {
                std::cerr << "execute_raw: get_db returned null for " << test.operation.view() << '\n';
                return;
            }

            const std::string sql = terminal_->sql();

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                std::cerr << "execute_raw: sqlite3_prepare_v2 failed: " << sqlite3_errmsg(db) << '\n';
                return;
            }

            bind_raw_params(stmt);

            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);
                step_raw(stmt);
            }

            sqlite3_finalize(stmt);
        }

      private:
        // ====================================================================
        // apply_aggregate — reflection-based dispatch via ^^ splice
        // ====================================================================
        // Resolve aggregate method by name via ^^ reflection.
        // ValidAggregate constrains the function name; when a field is present
        // it's further gated by has_field<Model>(...) for a clear call-site error.
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

        // ====================================================================
        // DISTINCT — index_sequence dispatch (scales to any MAX_FIELDS)
        // ====================================================================
        static auto build_distinct_stmt(auto& qs) {
            return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return qs.template distinct<dispatch_field<Model>(test.distinct.fields[Is].view())...>();
            }(std::make_index_sequence<test.distinct.field_count()>{});
        }

        // ====================================================================
        // GROUP BY — shared builder (index_sequence + apply_aggregate reuse)
        // ====================================================================
        // Returns the fully-configured group_by statement (with having + aggregate)
        static auto build_group_by_query(auto& qs) {
            auto gb = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return qs.template group_by<dispatch_field<Model>(test.group_by.fields[Is].view())...>();
            }(std::make_index_sequence<test.group_by.field_count()>{});

            if constexpr (test.group_by.having.enabled) {
                static_assert(has_field<Model>(test.group_by.having.field.view()), "HAVING field not found on model");
                constexpr auto hf = dispatch_field<Model>(test.group_by.having.field.view());
                // Pass a prvalue — `build_compare_expr` forwards into a
                // forwarding-reference parameter on `operator>` etc. A constexpr
                // lvalue would bind as `const int&` and mismatch `V&&`.
                gb = gb.having(
                        build_compare_expr<hf, test.group_by.having.op>(
                                auto{numeric_value<test.group_by.having.value>()}
                        )
                );
            }

            return apply_aggregate(gb);
        }

        // ====================================================================
        // SET OPERATIONS — build configured setop builder from ORM
        // Uses ^^ reflection splice — setop methods are template members
        // (template <bool F_ = Finalized_>), disambiguate with `template`.
        // ====================================================================
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

        // Shared by build_setop (ORM path) and bind_setop_params (raw path)
        // so both sides bind the exact same operand values. Drift here silently
        // makes the comparison unfair.
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

        // ====================================================================
        // build_terminal — shared builder, returns the terminal statement object
        // Must be defined after all helpers it calls (apply_aggregate, etc.)
        // ====================================================================
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

        // ====================================================================
        // run_terminal — executes the terminal statement (.execute() vs .select())
        // ====================================================================
        static auto run_terminal(auto& stmt) -> void {
            if constexpr (is_group_by_op() || is_distinct_op()) {
                stmt.select();
            } else {
                (void)stmt.execute();
            }
        }

        // Derive QuerySet type from build_qs — it may be finalized (<..., true>)
        // when ORDER BY / LIMIT / OFFSET shift the type, or the base type for setop.
        using QsType       = decltype(build_qs());
        using TerminalType = decltype(build_terminal(std::declval<QsType&>()));

        QsType                        query_qs_;
        std::optional<TerminalType>   terminal_;
        QuerySet<registry::FKRelated> related_qs_;

        // ====================================================================
        // Raw helpers — param binding, row extraction
        // ====================================================================
        static auto bind_raw_params(sqlite3_stmt* stmt) -> void {
            if constexpr (is_setop()) {
                bind_setop_params(stmt);
            } else {
                bind_where_params(stmt);
            }
        }

        // TODO: looks hard to read - separ by funcs - DRY
        static auto step_raw(sqlite3_stmt* stmt) -> void {
            if constexpr (is_aggregate_op()) {
                constexpr bool is_fp = (test.aggregate.func.view() == "avg");
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    if constexpr (is_fp) {
                        [[maybe_unused]] auto v = sqlite3_column_double(stmt, 0);
                    } else {
                        [[maybe_unused]] auto v = sqlite3_column_int64(stmt, 0);
                    }
                }
            } else if constexpr (is_first_op()) {
                sqlite3_step(stmt);
            } else if constexpr (is_get_op()) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {}
            } else if constexpr (is_distinct_op()) {
                // Mirror ORM distinct.cppm:266–291: plf::hive<tuple<field_types...>>.
                // Per-row: extract each DISTINCT column via raw_helpers::extract_value<T>,
                // build a tuple, insert into hive. Matches the ORM's allocator + read pattern
                // so the comparison stays fair. Covers both single- and multi-column DISTINCT
                // (ORM keeps a hive-of-T for N==1; we keep hive-of-tuple<T> for simplicity —
                // one extra move per row is negligible vs. the hive insert cost).
                using DistinctTuple = decltype([]<size_t... Is>(std::index_sequence<Is...>) {
                    return std::tuple<std::remove_cvref_t<
                            decltype(std::declval<Model>()
                                             .[:dispatch_field<Model>(test.distinct.fields[Is].view()):])>...>{};
                }(std::make_index_sequence<test.distinct.field_count()>{}));

                plf::hive<DistinctTuple> results;
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    results.insert([&]<size_t... Is>(std::index_sequence<Is...>) {
                        return std::make_tuple(
                                extract_value<std::tuple_element_t<Is, DistinctTuple>>(stmt, static_cast<int>(Is))...
                        );
                    }(std::make_index_sequence<test.distinct.field_count()>{}));
                }
            } else {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    [[maybe_unused]] auto row = storm::benchmark::extract_row<Model>(stmt);
                }
            }
        }

        static auto bind_setop_params(sqlite3_stmt* stmt) -> void {
            constexpr auto th = setop_thresholds();
            sqlite3_bind_int(stmt, 1, th.left);
            sqlite3_bind_int(stmt, 2, th.right);
        }

        // ====================================================================
        // WHERE parameter binding for raw execute — uses typed_value_as to
        // promote TypedValue to the field's native type, then raw_helpers::bind_value
        // (10 overloads covering int/int64/double/float/bool/string/blob).
        // ====================================================================
        template <size_t I> static auto bind_condition_params(sqlite3_stmt* stmt, int& param_idx) -> void {
            constexpr auto const&      cond    = test.where.conditions[I];
            constexpr auto             field_i = dispatch_field<WhereModel>(cond.field.view());
            constexpr std::string_view op      = cond.op.view();
            using FieldType                    = typename[:std::meta::type_of(field_i):];

            if constexpr (op == "LIKE") {
                sqlite3_bind_text(stmt, param_idx++, cond.values[0].as_string.c_str(), -1, SQLITE_TRANSIENT);
            } else if constexpr (op == "BETWEEN") {
                storm::benchmark::bind_value(stmt, param_idx++, typed_value_as<FieldType, cond.values[0]>());
                storm::benchmark::bind_value(stmt, param_idx++, typed_value_as<FieldType, cond.values[1]>());
            } else if constexpr (op == "IN") {
                [&]<size_t... Is>(std::index_sequence<Is...>) {
                    (storm::benchmark::bind_value(stmt, param_idx++, typed_value_as<FieldType, cond.values[Is]>()),
                     ...);
                }(std::make_index_sequence<cond.value_count>{});
            } else if constexpr (op == "IS NULL" || op == "IS NOT NULL") {
                // No parameters
            } else {
                storm::benchmark::bind_value(stmt, param_idx++, typed_value_as<FieldType, cond.values[0]>());
            }
        }

        static auto bind_where_params(sqlite3_stmt* stmt) -> void {
            if constexpr (!test.where.enabled) {
                return;
            }

            int param_idx = 1;
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                (bind_condition_params<Is>(stmt, param_idx), ...);
            }(std::make_index_sequence<test.where.condition_count>{});
        }
    };

} // namespace storm::benchmark
