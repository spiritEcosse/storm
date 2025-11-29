# Known Issues

## FK Field Bugs (5 failing tests - 2% failure rate)

These bugs were discovered during the plf::hive migration (2025-01). They are **NOT** related to the iterator changes - they are pre-existing issues with FK (foreign key) field population in the ORM's SELECT/JOIN logic.

### Issue 1: FK Field Initialization Bug
**Test:** `FKFieldTest.SelectWithFKFieldPartialPopulation`  
**Location:** tests/test_fk_fields.cpp:145, 149  
**Symptom:** Non-PK fields in FK objects show garbage values instead of default values
```
Expected: sender.age == 0
Actual:   sender.age == 1403774600  (uninitialized memory)
```
**Root Cause:** SELECT statement doesn't initialize non-selected FK object fields to default values

---

### Issue 2: JOIN FK Population Bug
**Test:** `FKFieldTest.JoinFullyPopulatesFKObject`  
**Symptom:** FK objects not fully populated during JOIN operations  
**Root Cause:** JOIN statement field extraction logic incomplete

---

### Issue 3: LEFT JOIN FK Handling Bug
**Test:** `FKFieldTest.LeftJoinReturnsAllMessages`  
**Symptom:** LEFT JOIN doesn't correctly handle FK fields  
**Root Cause:** LEFT JOIN logic doesn't properly extract FK object fields

---

### Issue 4: NULL FK Field Bug
**Test:** `NullableFKTest.LeftJoinWithNullFKField`  
**Symptom:** NULL FK fields not handled properly in LEFT JOIN  
**Root Cause:** LEFT JOIN doesn't initialize NULL FK fields correctly

---

### Issue 5: Mixed NULL/Valid FK Bug
**Test:** `NullableFKTest.LeftJoinWithMixedNullAndValidFKs`  
**Symptom:** Mixture of NULL and valid FK values not handled correctly  
**Root Cause:** LEFT JOIN logic doesn't properly distinguish NULL vs valid FK fields

---

## Summary

**Test Status:** 207/212 passing (98% pass rate)  
**Affected Area:** FK field population in SELECT/JOIN statements  
**Evidence:** Garbage values (e.g., 1403774600) indicate uninitialized memory  
**Next Steps:** Debug SELECT/JOIN statement field extraction logic for FK objects
