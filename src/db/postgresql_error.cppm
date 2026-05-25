module;

export module storm_db_postgresql_error;
import storm_db_concept;

export namespace storm::db::postgresql {

    // Error type — shared across backends, defined in storm_db_concept (issue #316).
    using Error = storm::db::Error;

} // namespace storm::db::postgresql
