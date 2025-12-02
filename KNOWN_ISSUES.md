# Known Issues

## FK Field Bugs ✅ ALL FIXED (2025-12-01)

These bugs were discovered during the plf::hive migration (2025-01). They were **NOT** related to the iterator changes - they were pre-existing issues with FK (foreign key) field population in the ORM's SELECT/JOIN logic.

**Status: All 5 issues fixed! 212/212 tests passing (100% pass rate)**

### Issue 1: FK Field Initialization Bug ✅ FIXED (2025-12-01)
**Test:** `FKFieldTest.SelectWithFKFieldPartialPopulation`
**Location:** tests/test_fk_fields.cpp:145, 149
**Symptom:** Non-PK fields in FK objects show garbage values instead of default values
```
Expected: sender.age == 0
Actual:   sender.age == 1403774600  (uninitialized memory)
```
**Root Cause:** SELECT statement didn't initialize FK objects before assigning PK value
**Fix:** Added `obj.[:member:] = FieldType{};` to default-construct FK object in select.cppm:220
**Status:** ✅ Test now passes

---

### Issue 2: JOIN FK Population Bug ✅ FIXED (2025-12-01)
**Tests:**
- `FKFieldTest.JoinFullyPopulatesFKObject`
- `FKFieldTest.LeftJoinReturnsAllMessages`
- `NullableFKTest.LeftJoinWithNullFKField`
- `NullableFKTest.LeftJoinWithMixedNullAndValidFKs`

**Symptom:** Non-JOINed FK fields showed garbage values during JOIN operations
**Root Cause:** JOIN statement only extracted JOINed FK fields, leaving non-JOINed FK fields uninitialized
**Fix:** Added `init_all_fk_fields()` to default-construct all FK objects before extraction in join.cppm:413
**Status:** ✅ All 4 tests now pass

**Note:** Issues 3, 4, and 5 were all manifestations of the same root cause and were fixed together.

---

## Summary

**Test Status:** 212/212 passing (100% pass rate) ✅ ALL BUGS FIXED!
**Fixed Issues:**
- Issue 1: FK field initialization in SELECT ✅
- Issue 2: JOIN FK population (fixed all 4 remaining failures) ✅

**What Was Fixed:**
1. **SELECT bug (1 test)**: Non-JOINed FK fields had garbage values
   - Fix: Default-construct FK objects before assigning PK in select.cppm:220
2. **JOIN bug (4 tests)**: Non-JOINed FK fields had garbage values during JOIN operations
   - Fix: Initialize all FK fields to defaults before extraction in join.cppm:413

**Result:** All FK field bugs resolved with 2 simple fixes!
