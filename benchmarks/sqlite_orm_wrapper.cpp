#include "sqlite_orm_wrapper.h"
#include <sqlite_orm/sqlite_orm.h>
#include <memory>
#include <string>

// Person struct for sqlite_orm
struct PersonSqliteOrm {
    int id;
    std::string name;
    int age;
};

using namespace sqlite_orm;

// Internal storage type
using Storage = decltype(make_storage("",
    make_table("Person",
        make_column("id", &PersonSqliteOrm::id, primary_key()),
        make_column("name", &PersonSqliteOrm::name),
        make_column("age", &PersonSqliteOrm::age)
    )
));

struct StorageWrapper {
    Storage storage;

    StorageWrapper(const char* db_path)
        : storage(make_storage(db_path,
            make_table("Person",
                make_column("id", &PersonSqliteOrm::id, primary_key()),
                make_column("name", &PersonSqliteOrm::name),
                make_column("age", &PersonSqliteOrm::age)
            )
        ))
    {
        storage.sync_schema();
    }
};

extern "C" {

sqlite_orm_storage_t sqlite_orm_init(const char* db_path) {
    try {
        auto* wrapper = new StorageWrapper(db_path);
        return static_cast<sqlite_orm_storage_t>(wrapper);
    } catch (...) {
        return nullptr;
    }
}

void sqlite_orm_insert_person(sqlite_orm_storage_t storage, int id, const char* name, int age) {
    if (!storage) return;

    auto* wrapper = static_cast<StorageWrapper*>(storage);
    try {
        PersonSqliteOrm person{id, std::string(name), age};
        wrapper->storage.insert(person);
    } catch (...) {
        // Silently ignore errors for benchmark simplicity
    }
}

void sqlite_orm_remove_person(sqlite_orm_storage_t storage, int id) {
    if (!storage) return;

    auto* wrapper = static_cast<StorageWrapper*>(storage);
    try {
        wrapper->storage.remove<PersonSqliteOrm>(id);
    } catch (...) {
        // Silently ignore errors for benchmark simplicity
    }
}

void sqlite_orm_update_person(sqlite_orm_storage_t storage, int id, const char* name, int age) {
    if (!storage) return;

    auto* wrapper = static_cast<StorageWrapper*>(storage);
    try {
        PersonSqliteOrm person{id, std::string(name), age};
        wrapper->storage.update(person);
    } catch (...) {
        // Silently ignore errors for benchmark simplicity
    }
}

void sqlite_orm_cleanup(sqlite_orm_storage_t storage) {
    if (!storage) return;

    auto* wrapper = static_cast<StorageWrapper*>(storage);
    delete wrapper;
}

void sqlite_orm_begin_transaction(sqlite_orm_storage_t storage) {
    if (!storage) return;

    auto* wrapper = static_cast<StorageWrapper*>(storage);
    try {
        wrapper->storage.begin_transaction();
    } catch (...) {
        // Silently ignore errors for benchmark simplicity
    }
}

void sqlite_orm_commit_transaction(sqlite_orm_storage_t storage) {
    if (!storage) return;

    auto* wrapper = static_cast<StorageWrapper*>(storage);
    try {
        wrapper->storage.commit();
    } catch (...) {
        // Silently ignore errors for benchmark simplicity
    }
}

} // extern "C"