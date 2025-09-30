#ifndef SQLITE_ORM_WRAPPER_H
#define SQLITE_ORM_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle for sqlite_orm storage
typedef void* sqlite_orm_storage_t;

// Initialize storage and create table
sqlite_orm_storage_t sqlite_orm_init(const char* db_path);

// Insert records
void sqlite_orm_insert_person(sqlite_orm_storage_t storage, int id, const char* name, int age);

// Remove a person by id
void sqlite_orm_remove_person(sqlite_orm_storage_t storage, int id);

// Select all persons - returns count of rows fetched
int sqlite_orm_select_all_persons(sqlite_orm_storage_t storage);

// Update a person by id
void sqlite_orm_update_person(sqlite_orm_storage_t storage, int id, const char* name, int age);

// Select all persons - returns count of rows fetched
int sqlite_orm_select_all_persons(sqlite_orm_storage_t storage);

// Cleanup
void sqlite_orm_cleanup(sqlite_orm_storage_t storage);

// Transaction control
void sqlite_orm_begin_transaction(sqlite_orm_storage_t storage);
void sqlite_orm_commit_transaction(sqlite_orm_storage_t storage);

#ifdef __cplusplus
}
#endif

#endif // SQLITE_ORM_WRAPPER_H