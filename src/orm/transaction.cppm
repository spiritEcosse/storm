module;

#include <meta>

export module storm_orm_transaction;

import <expected>;

export namespace storm::orm::utilities {

    // ============================================================================
    // Transaction Guard - RAII-style transaction management
    // ============================================================================
    //
    // Usage:
    //   auto txn = TransactionGuard<ConnType>::begin(*conn_);
    //   if (!txn) return std::unexpected(txn.error());
    //
    //   auto result = do_work();
    //   if (!result) return result;  // Destructor will ROLLBACK
    //
    //   return txn->commit();  // Explicit COMMIT, returns expected<void, Error>
    //
    // Destructor automatically ROLLBACKs if commit() was not called.
    // ============================================================================

    template <typename ConnType> class TransactionGuard {
        using Error = typename ConnType::Error;

        ConnType* conn_;
        bool      committed_ = false;

        explicit TransactionGuard(ConnType* conn) noexcept : conn_(conn) {}

      public:
        // Factory method - begins transaction, returns expected
        [[nodiscard]] static auto begin(ConnType& conn) noexcept -> std::expected<TransactionGuard, Error> {
            if (auto result = conn.execute("BEGIN TRANSACTION"); !result) {
                return std::unexpected(result.error());
            }
            return TransactionGuard(&conn);
        }

        // Move-only (no copying)
        TransactionGuard(const TransactionGuard&)                    = delete;
        auto operator=(const TransactionGuard&) -> TransactionGuard& = delete;

        TransactionGuard(TransactionGuard&& other) noexcept : conn_(other.conn_), committed_(other.committed_) {
            other.conn_ = nullptr; // Prevent double-rollback
        }

        auto operator=(TransactionGuard&& other) noexcept -> TransactionGuard& {
            if (this != &other) {
                rollback_if_needed();
                conn_       = other.conn_;
                committed_  = other.committed_;
                other.conn_ = nullptr;
            }
            return *this;
        }

        // Destructor - rollback if not committed
        ~TransactionGuard() {
            rollback_if_needed();
        }

        // Explicit commit - must be called for successful transaction
        [[nodiscard]] auto commit() noexcept -> std::expected<void, Error> {
            if (conn_ == nullptr || committed_) {
                return {}; // Already committed or moved-from
            }

            if (auto result = conn_->execute("COMMIT"); !result) {
                (void)conn_->execute("ROLLBACK");
                return std::unexpected(result.error());
            }

            committed_ = true;
            return {};
        }

      private:
        auto rollback_if_needed() noexcept -> void {
            if (conn_ && !committed_) {
                (void)conn_->execute("ROLLBACK");
            }
        }
    };

} // namespace storm::orm::utilities
