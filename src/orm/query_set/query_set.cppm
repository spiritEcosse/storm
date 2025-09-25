module;

#include <sqlite3.h>

export module storm_query_set;

import storm_statement_remove;
import storm_db_concept;

import <expected>;
import <string>;
import <span>;

export namespace storm {
    template <class T, storm::db::DatabaseConnection ConnType> class QuerySet {
        using Error = typename ConnType::Error;

      public:
        explicit QuerySet(ConnType& conn) : conn_(conn) {}

        std::expected<bool, Error> remove(const T& obj) {
            return execute_remove(std::span<const T>{&obj, 1});
        }

      private:
        [[nodiscard]] std::expected<bool, Error> execute_remove(std::span<const T> objects) const noexcept {
            return RemoveStatement<T, ConnType>(conn_).execute(objects);
        }

        ConnType& conn_;
    };
} // namespace storm
