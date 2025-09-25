module;

export module storm;

// Import and re-export all Storm modules
export import storm_meta;
export import storm_db_concept;
export import storm_db_sqlite;
export import storm_db_sqlite_adapter;
export import storm_statement_remove;
export import storm_query_set;

export namespace storm {
    // Storm ORM functionality with reflection support
    constexpr auto version = "0.1.0";
} // namespace storm
