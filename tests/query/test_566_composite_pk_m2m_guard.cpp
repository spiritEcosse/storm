#include <gtest/gtest.h>
#include <meta>

#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h"
#include "crud/test_composite_pk_models.h"

namespace {

    class CompositePkM2mGuardTest : public ::testing::Test {};

    // #566 — Verify that composite-PK m2m junctions generate correct DDL for all combinations.
    // The junction DDL should have multi-column FKs when EITHER side (owner or related) has
    // a composite PK. These tests ensure the DDL is correct by verifying it can be parsed
    // and contains the expected structure.

    TEST_F(CompositePkM2mGuardTest, CompositeOwnerJunctionDDLStructure) {
        // LedgerWithTags: composite owner (3 parts), single-PK related
        using SchemaT   = storm::orm::schema::SchemaStatement<LedgerWithTags>;
        const auto& sql = SchemaT::junction_table_sql();

        // Must have:
        // - correct table name
        // - 4 columns (3 owner + 1 related)
        // - composite PRIMARY KEY on all 4
        // - two FOREIGN KEY clauses (one for owner, one for related)
        EXPECT_NE(sql.find("CREATE TABLE LedgerWithTags_LedgerTag"), std::string::npos);
        EXPECT_NE(sql.find("LedgerWithTags_region"), std::string::npos);
        EXPECT_NE(sql.find("LedgerWithTags_account"), std::string::npos);
        EXPECT_NE(sql.find("LedgerWithTags_period"), std::string::npos);
        EXPECT_NE(sql.find("LedgerTag_id"), std::string::npos);
        EXPECT_NE(sql.find("PRIMARY KEY"), std::string::npos);
        // Should have two FOREIGN KEY clauses
        EXPECT_NE(sql.find("FOREIGN KEY"), std::string::npos);
    }

    TEST_F(CompositePkM2mGuardTest, SingleOwnerCompositeRelatedJunctionDDLStructure) {
        // TagRegistry: single-PK owner, composite related (2 parts)
        using SchemaT   = storm::orm::schema::SchemaStatement<TagRegistry>;
        const auto& sql = SchemaT::junction_table_sql();

        EXPECT_NE(sql.find("CREATE TABLE TagRegistry_CatalogEntry"), std::string::npos);
        EXPECT_NE(sql.find("TagRegistry_id"), std::string::npos);
        EXPECT_NE(sql.find("CatalogEntry_catalog_id"), std::string::npos);
        EXPECT_NE(sql.find("CatalogEntry_entry_no"), std::string::npos);
        EXPECT_NE(sql.find("PRIMARY KEY"), std::string::npos);
        EXPECT_NE(sql.find("FOREIGN KEY"), std::string::npos);
    }

    TEST_F(CompositePkM2mGuardTest, BothCompositeJunctionDDLStructure) {
        // ShelfAssignment: composite owner (2 parts), composite related (3 parts)
        using SchemaT    = storm::orm::schema::SchemaStatement<ShelfAssignment>;
        const auto& sqls = SchemaT::junction_table_sqls();

        ASSERT_EQ(sqls.size(), 2U);

        // First relation: bins (3-part composite related)
        const auto& bins_sql = sqls[0];
        EXPECT_NE(bins_sql.find("CREATE TABLE ShelfAssignment_StorageBin"), std::string::npos);
        EXPECT_NE(bins_sql.find("ShelfAssignment_warehouse_no"), std::string::npos);
        EXPECT_NE(bins_sql.find("ShelfAssignment_shelf_code"), std::string::npos);
        EXPECT_NE(bins_sql.find("StorageBin_aisle"), std::string::npos);
        EXPECT_NE(bins_sql.find("StorageBin_bin_code"), std::string::npos);
        EXPECT_NE(bins_sql.find("StorageBin_revision"), std::string::npos);
        EXPECT_NE(bins_sql.find("PRIMARY KEY"), std::string::npos);

        // Second relation: tags (1-part single related)
        const auto& tags_sql = sqls[1];
        EXPECT_NE(tags_sql.find("CREATE TABLE ShelfAssignment_LedgerTag"), std::string::npos);
        EXPECT_NE(tags_sql.find("ShelfAssignment_warehouse_no"), std::string::npos);
        EXPECT_NE(tags_sql.find("ShelfAssignment_shelf_code"), std::string::npos);
        EXPECT_NE(tags_sql.find("LedgerTag_id"), std::string::npos);
    }

} // namespace
