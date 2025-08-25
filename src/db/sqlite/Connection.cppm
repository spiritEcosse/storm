module;

// Module global fragment
#include <sqlite3.h>

// Define the module
export module storm.connection;

// Import standard header units
import <string>;
import <type_traits>;
import <cstdint>;

export class Connection {
public:
  // Transaction isolation levels
  enum class TransactionLevel {
    DEFERRED,  // Default in SQLite, doesn't acquire locks until needed
    IMMEDIATE, // Acquires write lock immediately, preventing other connections
               // from writing
    EXCLUSIVE  // Acquires all locks immediately, preventing other connections
               // from reading or writing
  };

  explicit Connection(const std::string &db_name);
  ~Connection();
  Connection(const Connection &) = delete;
  Connection &operator=(const Connection &) = delete;
  Connection(Connection &&other) noexcept;
  Connection &operator=(Connection &&other) noexcept;
  sqlite3 *get() const;
  bool is_open() const;
  std::int64_t last_insert_id() const;
  int get_affected_rows() const;

  // Transaction methods
  void begin_transaction(TransactionLevel level = TransactionLevel::DEFERRED);
  void commit();
  void rollback();
  bool in_transaction() const;

private:
  sqlite3 *db;
  bool transaction_active = false;

public:
  template <typename N> static std::string get_type_name() {
    if constexpr (std::is_same_v<N, std::string>) {
      return "VARCHAR";
    } else if constexpr (std::is_same_v<N, int>) {
      return "INTEGER";
    } else if constexpr (std::is_same_v<N, bool>) {
      return "BOOLEAN";
    } else if constexpr (std::is_same_v<N, float>) {
      return "REAL";
    } else if constexpr (std::is_same_v<N, double>) {
      return "DOUBLE PRECISION";
    } else {
      return "UNKNOWN";
    }
  }
};
