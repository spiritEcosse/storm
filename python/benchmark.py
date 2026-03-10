#!/usr/bin/env python3
"""Quick benchmark: raw sqlite3 vs Storm thin wrapper (_storm).

Measures wall-clock time for the same CRUD operations on an in-memory database.
Run from the build output directory where _storm*.so lives, or set PYTHONPATH.

    python3 python/benchmark.py
"""

import sqlite3
import sys
import time
from dataclasses import dataclass

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False

# ── Try importing the Storm binding ─────────────────────────────────────────
try:
    import _storm as storm
    from _storm import Person as StormPerson
    HAS_STORM = True
except ImportError:
    HAS_STORM = False
    print("[warn] _storm module not found — skipping Storm benchmarks.")
    print("       Build the module first (cmake --preset ninja-release && cmake --build --preset ninja-release)")
    print()


# ── Config ───────────────────────────────────────────────────────────────────
N_SINGLE   = 1_000   # single-row inserts per run
N_BULK     = 10_000  # rows in the bulk-insert run
N_SELECT   = 500     # select-all iterations (table size = N_BULK rows)
N_WHERE    = 1_000   # filtered-select iterations
WARMUP     = 3       # discard this many iterations before measuring


# ── Helpers ──────────────────────────────────────────────────────────────────
@dataclass
class Result:
    label: str
    ops: int
    elapsed_s: float

    @property
    def ops_per_sec(self) -> float:
        return self.ops / self.elapsed_s if self.elapsed_s > 0 else 0

    @property
    def us_per_op(self) -> float:
        return (self.elapsed_s / self.ops) * 1e6 if self.ops > 0 else 0


def header(title: str) -> None:
    print(f"\n{'─' * 60}")
    print(f"  {title}")
    print(f"{'─' * 60}")
    print(f"  {'Benchmark':<32}  {'ops/s':>10}  {'µs/op':>8}")
    print(f"  {'─'*32}  {'─'*10}  {'─'*8}")


def row(r: Result) -> None:
    print(f"  {r.label:<32}  {r.ops_per_sec:>10,.0f}  {r.us_per_op:>8.2f}")


def ratio(a: Result, b: Result) -> None:
    if b.ops_per_sec > 0:
        r = a.ops_per_sec / b.ops_per_sec
        pct = (r - 1) * 100
        sign = "+" if pct >= 0 else ""
        print(f"\n  Storm vs raw: {r:.2f}x  ({sign}{pct:.1f}%)")


# ── Raw sqlite3 benchmarks ───────────────────────────────────────────────────
def raw_single_insert(n: int) -> float:
    con = sqlite3.connect(":memory:", autocommit=True)
    con.execute("CREATE TABLE pyperson (id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER NOT NULL)")
    # warmup
    for i in range(WARMUP):
        cur = con.execute("INSERT INTO pyperson (name, age) VALUES (?, ?)", (f"W{i}", i))
        _ = cur.lastrowid
    t0 = time.perf_counter()
    for i in range(n):
        cur = con.execute("INSERT INTO pyperson (name, age) VALUES (?, ?)", (f"Person{i}", i % 100))
        _ = cur.lastrowid
    return time.perf_counter() - t0


def raw_bulk_insert(n: int) -> float:
    con = sqlite3.connect(":memory:")
    con.execute("CREATE TABLE pyperson (id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER NOT NULL)")
    con.commit()
    rows = [(f"Person{i}", i % 100) for i in range(n)]
    t0 = time.perf_counter()
    con.executemany("INSERT INTO pyperson (name, age) VALUES (?, ?)", rows)
    con.commit()
    return time.perf_counter() - t0


def raw_select_all(n_iters: int, table_size: int) -> float:
    con = sqlite3.connect(":memory:")
    con.execute("CREATE TABLE pyperson (id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER NOT NULL)")
    rows = [(f"Person{i}", i % 100) for i in range(table_size)]
    con.executemany("INSERT INTO pyperson (name, age) VALUES (?, ?)", rows)
    con.commit()
    # warmup
    for _ in range(WARMUP):
        _ = con.execute("SELECT id, name, age FROM pyperson").fetchall()
    t0 = time.perf_counter()
    for _ in range(n_iters):
        _ = con.execute("SELECT id, name, age FROM pyperson").fetchall()
    return time.perf_counter() - t0


def raw_select_where(n_iters: int, table_size: int) -> float:
    con = sqlite3.connect(":memory:")
    con.execute("CREATE TABLE pyperson (id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER NOT NULL)")
    rows = [(f"Person{i}", i % 100) for i in range(table_size)]
    con.executemany("INSERT INTO pyperson (name, age) VALUES (?, ?)", rows)
    con.commit()
    for _ in range(WARMUP):
        _ = con.execute("SELECT id, name, age FROM pyperson WHERE age > ?", (30,)).fetchall()
    t0 = time.perf_counter()
    for _ in range(n_iters):
        _ = con.execute("SELECT id, name, age FROM pyperson WHERE age > ?", (30,)).fetchall()
    return time.perf_counter() - t0


def raw_count(n_iters: int, table_size: int) -> float:
    con = sqlite3.connect(":memory:")
    con.execute("CREATE TABLE pyperson (id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER NOT NULL)")
    rows = [(f"Person{i}", i % 100) for i in range(table_size)]
    con.executemany("INSERT INTO pyperson (name, age) VALUES (?, ?)", rows)
    con.commit()
    for _ in range(WARMUP):
        con.execute("SELECT COUNT(*) FROM pyperson").fetchone()
    t0 = time.perf_counter()
    for _ in range(n_iters):
        con.execute("SELECT COUNT(*) FROM pyperson").fetchone()
    return time.perf_counter() - t0


# ── Storm benchmarks ─────────────────────────────────────────────────────────
def storm_single_insert(n: int) -> float:
    storm.connect(":memory:")
    storm.create_table()
    for i in range(WARMUP):
        storm.insert(StormPerson(name=f"W{i}", age=i))
    t0 = time.perf_counter()
    for i in range(n):
        storm.insert(StormPerson(name=f"Person{i}", age=i % 100))
    return time.perf_counter() - t0


def storm_fast_insert(n: int) -> float:
    storm.connect(":memory:")
    storm.create_table()
    for i in range(WARMUP):
        storm.fast_insert(f"W{i}", i)
    t0 = time.perf_counter()
    for i in range(n):
        storm.fast_insert(f"Person{i}", i % 100)
    return time.perf_counter() - t0


def storm_fast_insert_many(n: int) -> float:
    storm.connect(":memory:")
    storm.create_table()
    names = [f"Person{i}" for i in range(n)]
    ages = [i % 100 for i in range(n)]
    t0 = time.perf_counter()
    storm.fast_insert_many(names, ages)
    return time.perf_counter() - t0


def storm_bulk_insert(n: int) -> float:
    storm.connect(":memory:")
    storm.create_table()
    people = [StormPerson(name=f"Person{i}", age=i % 100) for i in range(n)]
    t0 = time.perf_counter()
    storm.bulk_insert(people)
    return time.perf_counter() - t0


def storm_select_all(n_iters: int, table_size: int) -> float:
    storm.connect(":memory:")
    storm.create_table()
    storm.bulk_insert([StormPerson(name=f"Person{i}", age=i % 100) for i in range(table_size)])
    for _ in range(WARMUP):
        _ = storm.select()
    t0 = time.perf_counter()
    for _ in range(n_iters):
        _ = storm.select()
    return time.perf_counter() - t0


def storm_select_where(n_iters: int, table_size: int) -> float:
    storm.connect(":memory:")
    storm.create_table()
    storm.bulk_insert([StormPerson(name=f"Person{i}", age=i % 100) for i in range(table_size)])
    for _ in range(WARMUP):
        _ = storm.select_where(StormPerson.c_age > 30)
    t0 = time.perf_counter()
    for _ in range(n_iters):
        _ = storm.select_where(StormPerson.c_age > 30)
    return time.perf_counter() - t0


def storm_count(n_iters: int, table_size: int) -> float:
    storm.connect(":memory:")
    storm.create_table()
    storm.bulk_insert([StormPerson(name=f"Person{i}", age=i % 100) for i in range(table_size)])
    for _ in range(WARMUP):
        _ = storm.count()
    t0 = time.perf_counter()
    for _ in range(n_iters):
        _ = storm.count()
    return time.perf_counter() - t0


def storm_select_array(n_iters: int, table_size: int) -> float:
    storm.connect(":memory:")
    storm.create_table()
    storm.bulk_insert([StormPerson(name=f"Person{i}", age=i % 100) for i in range(table_size)])
    for _ in range(WARMUP):
        _ = storm.select_array()
    t0 = time.perf_counter()
    for _ in range(n_iters):
        _ = storm.select_array()
    return time.perf_counter() - t0


def raw_select_array(n_iters: int, table_size: int) -> float:
    """raw sqlite3 fetchall() then pack into a numpy structured array."""
    con = sqlite3.connect(":memory:")
    con.execute("CREATE TABLE pyperson (id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER NOT NULL)")
    rows = [(f"Person{i}", i % 100) for i in range(table_size)]
    con.executemany("INSERT INTO pyperson (name, age) VALUES (?, ?)", rows)
    con.commit()
    dtype = np.dtype([("id", "<i4"), ("name", "S64"), ("age", "<i4")])
    for _ in range(WARMUP):
        tuples = con.execute("SELECT id, name, age FROM pyperson").fetchall()
        _ = np.array(tuples, dtype=dtype)
    t0 = time.perf_counter()
    for _ in range(n_iters):
        tuples = con.execute("SELECT id, name, age FROM pyperson").fetchall()
        _ = np.array(tuples, dtype=dtype)
    return time.perf_counter() - t0


# ── Main ─────────────────────────────────────────────────────────────────────
def main() -> None:
    print("Storm Python bindings — quick benchmark")
    print(f"  single inserts : {N_SINGLE:,}")
    print(f"  bulk rows      : {N_BULK:,}")
    print(f"  select iters   : {N_SELECT:,}  (table = {N_BULK:,} rows)")
    print(f"  where iters    : {N_WHERE:,}   (table = {N_BULK:,} rows)")

    # ── INSERT (single) ───────────────────────────────────────────────────
    header("INSERT single row")
    r_raw = Result("raw sqlite3", N_SINGLE, raw_single_insert(N_SINGLE))
    row(r_raw)
    if HAS_STORM:
        r_storm = Result("Storm insert(Person(...))", N_SINGLE, storm_single_insert(N_SINGLE))
        row(r_storm)
        r_fast = Result("Storm fast_insert(name, age)", N_SINGLE, storm_fast_insert(N_SINGLE))
        row(r_fast)
        r_many = Result("Storm fast_insert_many (C++ loop)", N_SINGLE, storm_fast_insert_many(N_SINGLE))
        row(r_many)
        ratio(r_many, r_raw)

    # ── INSERT bulk ───────────────────────────────────────────────────────
    header(f"INSERT bulk ({N_BULK:,} rows, single call)")
    r_raw = Result("raw sqlite3 executemany", N_BULK, raw_bulk_insert(N_BULK))
    row(r_raw)
    if HAS_STORM:
        r_storm = Result("Storm bulk_insert", N_BULK, storm_bulk_insert(N_BULK))
        row(r_storm)
        ratio(r_storm, r_raw)

    # ── SELECT all ────────────────────────────────────────────────────────
    header(f"SELECT * (table={N_BULK:,} rows, {N_SELECT:,} iterations)")
    r_raw = Result("raw sqlite3 → list[tuple]", N_SELECT, raw_select_all(N_SELECT, N_BULK))
    row(r_raw)
    if HAS_NUMPY:
        r_raw_np = Result("raw sqlite3 → numpy array", N_SELECT, raw_select_array(N_SELECT, N_BULK))
        row(r_raw_np)
    if HAS_STORM:
        r_storm = Result("Storm select() → list[Person]", N_SELECT, storm_select_all(N_SELECT, N_BULK))
        row(r_storm)
        if HAS_NUMPY:
            r_arr = Result("Storm select_array() → ndarray", N_SELECT, storm_select_array(N_SELECT, N_BULK))
            row(r_arr)
            ratio(r_arr, r_raw)

    # ── SELECT WHERE ─────────────────────────────────────────────────────
    header(f"SELECT WHERE age>30 (table={N_BULK:,} rows, {N_WHERE:,} iterations)")
    r_raw = Result("raw sqlite3", N_WHERE, raw_select_where(N_WHERE, N_BULK))
    row(r_raw)
    if HAS_STORM:
        r_storm = Result("Storm select_where()", N_WHERE, storm_select_where(N_WHERE, N_BULK))
        row(r_storm)
        ratio(r_storm, r_raw)

    # ── COUNT ─────────────────────────────────────────────────────────────
    header(f"COUNT(*) (table={N_BULK:,} rows, {N_WHERE:,} iterations)")
    r_raw = Result("raw sqlite3", N_WHERE, raw_count(N_WHERE, N_BULK))
    row(r_raw)
    if HAS_STORM:
        r_storm = Result("Storm count()", N_WHERE, storm_count(N_WHERE, N_BULK))
        row(r_storm)
        ratio(r_storm, r_raw)

    print(f"\n{'─' * 60}")
    print("  Done.")


if __name__ == "__main__":
    main()
