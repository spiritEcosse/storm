#pragma once

/**
 * @file test_write_runner.h
 * @brief InsertRunner, UpdateRunner, RemoveRunner for UnifiedTestCase.
 *
 * Include AFTER `import storm;`, test_models.h, test_seed_helpers.h,
 * test_query_runner_base.h.
 */

#include "test_query_runner_base.h"
#include "test_seed_helpers.h"

namespace storm::test {

// ---------------------------------------------------------------------------
// InsertRunner -- insert_one / insert_batch
// ---------------------------------------------------------------------------
template <typename Model, typename ConnType> class InsertRunner : public QueryRunnerBase<Model, ConnType> {
  public:
    template <const auto &Tc> auto run() -> void {
        if constexpr (Tc.query_type == "insert_one") {
            auto r = this->qs_.insert(make_record<Model>(0)).execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            auto cnt = this->qs_.count().get();
            ASSERT_TRUE(cnt.has_value()) << cnt.error().message();
            EXPECT_EQ(static_cast<int>(cnt.value()), Tc.expected.count);

        } else if constexpr (Tc.query_type == "insert_batch") {
            std::vector<Model> batch;
            batch.reserve(static_cast<size_t>(Tc.insert_count));
            for (int i = 0; i < Tc.insert_count; ++i)
                batch.push_back(make_record<Model>(i));
            auto r = this->qs_.insert(std::span<const Model>(batch)).execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            auto cnt = this->qs_.count().get();
            ASSERT_TRUE(cnt.has_value()) << cnt.error().message();
            EXPECT_EQ(static_cast<int>(cnt.value()), Tc.expected.count);
        }
    }
};

// ---------------------------------------------------------------------------
// UpdateRunner -- update_batch
// ---------------------------------------------------------------------------
template <typename Model, typename ConnType> class UpdateRunner : public QueryRunnerBase<Model, ConnType> {
  public:
    template <const auto &Tc> auto run() -> void {
        // Seed initial dataset
        std::vector<Model> initial;
        initial.reserve(static_cast<size_t>(Tc.dataset_size));
        for (int i = 0; i < Tc.dataset_size; ++i)
            initial.push_back(make_record<Model>(i));
        auto ins = this->qs_.insert(std::span<const Model>(initial)).execute();
        ASSERT_TRUE(ins.has_value()) << ins.error().message();

        // Re-fetch to get actual IDs
        auto sel = this->qs_.select().execute();
        ASSERT_TRUE(sel.has_value()) << sel.error().message();

        // Update first update_count records
        std::vector<Model> to_update;
        to_update.reserve(static_cast<size_t>(Tc.update_count));
        size_t n = 0;
        for (const auto &r : sel.value()) {
            if (n++ >= static_cast<size_t>(Tc.update_count))
                break;
            to_update.push_back(make_updated_record<Model>(r));
        }
        if (!to_update.empty()) {
            auto upd = this->qs_.update(std::span<const Model>(to_update)).execute();
            ASSERT_TRUE(upd.has_value()) << upd.error().message();
        }

        // Count records still with original prefix
        auto after = this->qs_.select().execute();
        ASSERT_TRUE(after.has_value()) << after.error().message();
        int unchanged_count = 0;
        for (const auto &r : after.value())
            if (is_original_record<Model>(r))
                ++unchanged_count;
        EXPECT_EQ(unchanged_count, Tc.expected.unchanged);
    }
};

// ---------------------------------------------------------------------------
// RemoveRunner -- remove_all / remove_where / remove_batch
// ---------------------------------------------------------------------------
template <typename Model, typename ConnType> class RemoveRunner : public QueryRunnerBase<Model, ConnType> {
  public:
    template <const auto &Tc> auto run() -> void {
        if constexpr (Tc.query_type == "remove_all") {
            // Seed dataset_size records, then remove all
            std::vector<Model> initial;
            initial.reserve(static_cast<size_t>(Tc.dataset_size));
            for (int i = 0; i < Tc.dataset_size; ++i)
                initial.push_back(make_record<Model>(i));
            auto ins = this->qs_.insert(std::span<const Model>(initial)).execute();
            ASSERT_TRUE(ins.has_value()) << ins.error().message();

            auto rem = this->qs_.remove_all().execute();
            ASSERT_TRUE(rem.has_value()) << rem.error().message();

            auto cnt = this->qs_.count().get();
            ASSERT_TRUE(cnt.has_value()) << cnt.error().message();
            EXPECT_EQ(static_cast<int>(cnt.value()), Tc.expected.remaining);

        } else if constexpr (Tc.query_type == "remove_batch") {
            // Seed dataset_size records
            std::vector<Model> initial;
            initial.reserve(static_cast<size_t>(Tc.dataset_size));
            for (int i = 0; i < Tc.dataset_size; ++i)
                initial.push_back(make_record<Model>(i));
            auto ins = this->qs_.insert(std::span<const Model>(initial)).execute();
            ASSERT_TRUE(ins.has_value()) << ins.error().message();

            // Re-fetch to get real IDs
            auto sel = this->qs_.select().execute();
            ASSERT_TRUE(sel.has_value()) << sel.error().message();

            // Remove first remove_count records
            std::vector<Model> to_remove;
            to_remove.reserve(static_cast<size_t>(Tc.remove_count));
            size_t n = 0;
            for (const auto &r : sel.value()) {
                if (n++ >= static_cast<size_t>(Tc.remove_count))
                    break;
                to_remove.push_back(r);
            }
            auto rem = this->qs_.remove(std::span<const Model>(to_remove)).execute();
            ASSERT_TRUE(rem.has_value()) << rem.error().message();

            auto cnt = this->qs_.count().get();
            ASSERT_TRUE(cnt.has_value()) << cnt.error().message();
            EXPECT_EQ(static_cast<int>(cnt.value()), Tc.expected.remaining);
        }
    }
};

} // namespace storm::test
