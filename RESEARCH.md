# How Databases Work Internally — A Deep Research Guide

> A from-the-ground-up reference for building your own database.
> Written so you can read top-to-bottom and end up with a mental model
> complete enough to implement every layer yourself.

---

## Table of Contents

1. [What a database actually is](#1-what-a-database-actually-is)
2. [High-level architecture](#2-high-level-architecture)
3. [The storage layer: files, pages, records](#3-the-storage-layer-files-pages-records)
4. [Buffer pool (page cache)](#4-buffer-pool-page-cache)
5. [Index structures](#5-index-structures)
6. [Row stores vs. column stores (storage models)](#6-row-stores-vs-column-stores-storage-models)
7. [Log-Structured Merge (LSM) engines](#7-log-structured-merge-lsm-engines)
8. [Query processing](#8-query-processing)
9. [Query optimization](#9-query-optimization)
10. [Transactions and ACID](#10-transactions-and-acid)
11. [Concurrency control](#11-concurrency-control)
12. [Crash recovery and durability (WAL / ARIES)](#12-crash-recovery-and-durability-wal--aries)
13. [The client/server and wire protocol layer](#13-the-clientserver-and-wire-protocol-layer)
14. [Beyond a single node (brief)](#14-beyond-a-single-node-brief)
15. [Suggested implementation roadmap](#15-suggested-implementation-roadmap)
16. [Further reading](#16-further-reading)

---

## 1. What a database actually is

At its core a database is **a program that manages data on persistent storage so
that many clients can safely query and modify it concurrently without losing
data on a crash.**

Everything a DBMS does is in service of that sentence. Let's break it into the
guarantees it has to give:

- **Persistence**: data survives process exit / machine crash / power loss.
- **Efficient access**: you can find a specific row (or a range of rows) without
  reading the whole dataset.
- **Concurrency**: many transactions can run at the same time as if they were
  running alone.
- **Atomicity**: a transaction either happens completely or not at all.
- **Recovery**: after a crash, the DB comes back up in a consistent state.
- **A query language**: users describe *what* they want, the DB figures out
  *how* to get it.

A database is essentially a giant layered cake built to provide those
guarantees over a block-oriented storage device (disk/SSD) which is millions of
times slower than RAM.

---

## 2. High-level architecture

Almost every RDBMS (Postgres, MySQL/InnoDB, SQLite, SQL Server, Oracle)
is structured in the same layers, top to bottom:

```
   ┌───────────────────────────────────────────────┐
   │  Client / Network layer (wire protocol)       │  SQL strings in, result sets out
   ├───────────────────────────────────────────────┤
   │  Parser  →  Analyzer/Binder  →  Rewriter      │  Turn SQL into an AST / logical plan
   ├───────────────────────────────────────────────┤
   │  Query Planner / Optimizer                    │  Pick the cheapest physical plan
   ├───────────────────────────────────────────────┤
   │  Execution Engine (operators)                 │  Scan, Filter, Join, Aggregate...
   ├───────────────────────────────────────────────┤
   │  Transaction / Concurrency Manager            │  Isolation, locks, MVCC
   ├───────────────────────────────────────────────┤
   │  Access Methods (indexes)                     │  B+Tree, Hash, LSM
   ├───────────────────────────────────────────────┤
   │  Buffer Pool Manager                          │  In-memory page cache
   ├───────────────────────────────────────────────┤
   │  Log Manager (WAL)                            │  Durability & recovery
   ├───────────────────────────────────────────────┤
   │  Disk / File Manager                          │  Pages on files, raw I/O
   └───────────────────────────────────────────────┘
```

A query flows top-down; a page read flows up from disk. The *log* sits beside
everything: before any page is modified in a way the user can see, the
change must be described in the log and the log flushed to disk
(**Write-Ahead Logging**).

If you build your DB layer-by-layer in the order above, each layer only
depends on the ones below it.

---

## 3. The storage layer: files, pages, records

### 3.1 Why pages?

Disks (and SSDs) are **block devices**. You can't read "one byte" from them;
the OS reads a fixed-size block. Even `mmap`'d reads fault in by page
(usually 4 KiB). So every DB picks a **page size** (commonly 4 KiB, 8 KiB, or
16 KiB) and makes that the unit of I/O.

A database file is just **an array of fixed-size pages**, numbered
`0, 1, 2, …, N-1`. Given a page number, you know the byte offset: `page_no *
page_size`. This is called a **heap file** (the file itself doesn't impose
order; it's just storage).

### 3.2 Slotted pages (the canonical record layout)

Records are variable-length (`VARCHAR`, nullable columns, etc.), so you can't
just pack them end-to-end — deleting or updating one would shift everything
else. The near-universal solution is the **slotted page**:

```
 ┌─────────────────────────────────────────────────────────┐
 │ header │ slot[0] slot[1] slot[2] ...  →              ←  │
 │        │                                                │
 │        │                               ...  record2     │
 │        │                         record1                │
 │        │                record0                         │
 └─────────────────────────────────────────────────────────┘
   fixed     grows right                   grows left
   header    (slot directory)              (record data)
```

- Header: page id, free-space pointer, LSN (see WAL), slot count, checksum.
- **Slot directory** grows from the front; each slot stores `(offset, length)`
  (and maybe flags like "tombstone").
- **Records** grow from the back of the page.
- Free space is the gap in the middle.

Addressing: a record is identified by a **RID (record id) = (page_id,
slot_no)**. Even if the record moves within the page during compaction, its
slot number doesn't change — you just update the slot's offset. That way
indexes can keep pointing at the RID.

**Deletes** mark the slot as dead (tombstone). **Updates** that fit in place
rewrite; ones that don't either compact the page or migrate the record to
another page leaving a forwarding pointer (MySQL/InnoDB does this; it's called
an "overflow" or "off-page" record).

### 3.3 Record (tuple) layout

Within a record, typically:

- A small **header** with: a null bitmap (1 bit per column), a transaction id
  (for MVCC), length info.
- Fixed-width columns first (`INT`, `BIGINT`, `TIMESTAMP`) — fast to address
  by offset.
- Variable-width columns last, with an offset array to find them.

Values that don't fit inline (large `TEXT`/`BLOB`) go into a separate overflow
area; the record stores a pointer. Postgres calls this **TOAST**.

### 3.4 File organization

A table can be laid out as:

- **Heap file**: unordered pages, RIDs are stable. Most common (Postgres,
  InnoDB for secondary storage).
- **Sorted file (ISAM)**: rows sorted by some key. Great for range scans, bad
  for inserts.
- **Index-organized table / clustered index**: the table *is* the leaf of a
  B+Tree keyed by PK. MySQL/InnoDB and SQL Server do this. Lookups by PK are
  one tree traversal with no extra indirection, but secondary indexes must
  store the PK (not a RID), and PK updates are expensive.

You can start with plain heap files.

### 3.5 Free-space tracking

You need to know which pages have room for a new record. Typical approaches:

- **FSM (Free Space Map)**: a separate page (or tree of pages) that stores
  the free bytes available on each data page, so INSERT can find a page fast.
- **Linked list of pages with free space.**
- **Bitmap pages**: 1 bit per data page = "has free space yes/no".

---

## 4. Buffer pool (page cache)

Disk I/O is ~100,000× slower than RAM. So every DB keeps a pool of page-sized
**frames** in memory and caches disk pages in them.

### 4.1 Core structures

- **Frame table / pool**: an array of `N` frames, each = one page worth of
  memory.
- **Page table / hash map**: `page_id → frame_id`. Tells you "is page X
  already in memory?"
- Per-frame metadata: `page_id`, **dirty bit**, **pin count**, and
  replacement-policy state.

### 4.2 The contract

Higher layers call something like:

```
frame  = bufferpool.pin(page_id)   // loads from disk if needed
... read / modify frame.data ...
bufferpool.unpin(page_id, dirty=true|false)
```

Rules:

- A page with `pin_count > 0` **cannot be evicted** — someone is reading
  or writing it.
- When you mark a page dirty, you also update its **LSN** (from the WAL).
- `flush(page_id)` writes a dirty page back to disk.
- `flush_all()` happens at checkpoints and shutdown.

### 4.3 Page replacement policies

When the pool is full and you need to bring in a new page, you must evict
one. Classic choices:

- **LRU (Least Recently Used)**: good in theory, but one big sequential scan
  pollutes the cache by pushing hot pages out.
- **Clock / Second-chance**: approximates LRU using a reference bit; O(1)
  and cheap. Postgres uses a variant.
- **LRU-K / ARC / 2Q**: track access history to resist scan pollution.
- **MRU (Most Recently Used)**: surprisingly useful *during* a large scan
  (the page you just used is the one least likely to be used again).

### 4.4 Writes: `FORCE` vs. `NO-FORCE`, `STEAL` vs. `NO-STEAL`

Two famous axes (Haerder & Reuter, 1983):

- **FORCE**: flush all pages a txn touched at commit time.
  **NO-FORCE**: don't; rely on the log.
- **STEAL**: allow writing uncommitted (dirty) pages to disk.
  **NO-STEAL**: never let an uncommitted change reach disk.

Almost every real DB is **STEAL + NO-FORCE** because it's the best for
performance. The price is that both **REDO** and **UNDO** logic are
required in recovery. (Details in §12.)

### 4.5 Prefetching / async I/O

For large scans, ask the OS to read the next few pages *before* you need
them (`posix_fadvise`, async I/O, `io_uring`). Hides disk latency.

---

## 5. Index structures

An index is any auxiliary structure that maps **key → location(s) of
matching rows** faster than scanning the table. The main families:

### 5.1 B+Tree (the workhorse)

The B+Tree is used by basically every major RDBMS (Postgres, MySQL,
SQLite, Oracle, SQL Server).

Key properties:

- **Balanced**: all leaves are at the same depth.
- **High fanout**: each internal node holds many keys (hundreds). A typical
  B+Tree with 4 levels indexes billions of keys.
- **Leaves form a linked list**: range scans are cheap (jump to the first
  matching leaf, then walk the list).
- **Only leaves hold data / RIDs**; internal nodes hold only routing keys.
  (This is the "+" — different from a plain B-Tree where internal nodes hold
  data too.)

Layout of one B+Tree node (which is one page):

```
 ┌──────────────────────────────────────────────────────────┐
 │ header │ key_0 ptr_0 key_1 ptr_1 ... key_{k-1} ptr_{k-1} │  (internal)
 └──────────────────────────────────────────────────────────┘

 ┌───────────────────────────────────────────────────────────┐
 │ header │ key_0 rid_0 key_1 rid_1 ... │ next_leaf │ prev   │  (leaf)
 └───────────────────────────────────────────────────────────┘
```

**Operations**:

- **Search**: binary-search each node's keys, follow the right child.
  `O(log_B n)` disk reads where B ≈ fanout.
- **Insert**: find the leaf, insert in sorted order. If the leaf overflows,
  **split** it in half, push the middle key up to the parent. Parent may in
  turn split, up to the root (root split grows the tree by 1 level).
- **Delete**: remove key. If the node is less than half-full, either
  **borrow** from a sibling or **merge** with it. Merges can propagate up.
- **Range scan**: descend to the first key ≥ lower bound, then walk the
  leaf linked list until you pass the upper bound.

**Concurrency (crucial and subtle)**:

Naive locking of the root kills throughput. Real implementations use
**latch crabbing / lock coupling**: when descending, latch the child, then
release the parent *if the child is "safe"* (won't split/merge). For writes,
only release when the child is safe; otherwise hold latches up to the highest
node that might change.

More advanced: **B-link trees** (Lehman & Yao, 1981) add right-sibling
pointers so readers never need to hold latches on internal nodes while
descending; this is what Postgres uses.

**Variants & tricks**:

- **Prefix compression** in internal nodes (store only the distinguishing
  prefix of each key).
- **Suffix truncation** of routing keys.
- **Bulk loading** (build bottom-up) when creating an index on an existing
  table — vastly faster than inserting one key at a time.

### 5.2 Hash index

Maps key → bucket via a hash function. `O(1)` expected for equality
lookups, useless for ranges. Used for in-memory tables, hash joins, and
some on-disk structures (PG has hash indexes; they're rarely the best
choice).

Important variants:

- **Static hashing + linear probing / chaining** — simple but fixed size.
- **Extendible hashing** (Fagin): a directory doubles when a bucket
  overflows. Good for on-disk.
- **Linear hashing** (Litwin): grows one bucket at a time; no directory
  doubling spikes.

### 5.3 LSM Tree (see §7 for the full treatment)

Write-optimized. Used by RocksDB, LevelDB, Cassandra, HBase, ScyllaDB,
SQLite4 draft, and (optionally) MySQL MyRocks.

### 5.4 Bloom filters

Not an index on their own — a *companion*. A Bloom filter tells you
"this key is **definitely not** in this set" or "**probably is**." Used in
LSM trees so you can skip reading SSTables that clearly don't contain
the key. Also used by HBase, Cassandra, Postgres extension `bloom`.

### 5.5 Inverted index

For full-text search: `word → list of document ids that contain it`. Core
structure of Lucene/Elasticsearch, Postgres `tsvector`/GIN.

### 5.6 R-Tree / GiST / spatial indexes

For multi-dimensional data (geometry). Each node stores a bounding box;
searches prune subtrees whose box doesn't intersect the query.

### 5.7 Skip lists

Probabilistic balanced structure. Excellent as the **in-memory** part of
an LSM tree (the "memtable") because they support concurrent inserts
with simple lock-free algorithms. RocksDB uses a skip list memtable.

### 5.8 Primary vs. secondary indexes; clustered vs. non-clustered

- **Primary index**: defined on the PK. If the table is *index-organized*,
  the PK index leaves **are** the rows. Otherwise, leaves hold RIDs
  pointing into the heap.
- **Secondary index**: on any other column. Leaves hold RIDs (heap table)
  or PK values (clustered table).
- **Covering index / included columns**: the leaf stores extra columns so
  the query can be answered from the index alone ("index-only scan"),
  no heap fetch.

---

## 6. Row stores vs. column stores (storage models)

### 6.1 Row-oriented (N-ary Storage Model, NSM)

All columns of one row stored together. Great for **OLTP**:

- Read/write a whole row by PK: one page touch.
- Insert: append one record.
- Bad for analytics: to `SUM(salary)` over 1B rows, you still read every
  byte of every row.

This is what Postgres, MySQL, SQLite do by default. **Start here.**

### 6.2 Column-oriented (Decomposition Storage Model, DSM)

Each column stored in its own file, row `i` of column `A` at offset
`i * sizeof(A)` (or a pointer into a dictionary).

- Great for **OLAP**: scans touch only the needed columns.
- Compression is *much* better (values in a column are similar — use
  dictionary, run-length, bit-packing, delta encoding, Gorilla for floats).
- Vectorized execution operates on column batches ("vectors") of ~1024
  values at a time, pipelining through CPU cache — orders of magnitude
  faster than row-at-a-time.

Used by: DuckDB, ClickHouse, Snowflake, BigQuery, Redshift, Vertica,
Parquet/ORC files, Apache Arrow.

### 6.3 Hybrid (PAX)

Within each page, group values by column. Still one page per row group,
but cache-friendly per-column access. Apache Arrow's file format and
Parquet's row-group layout are both PAX-flavored.

---

## 7. Log-Structured Merge (LSM) engines

LSM trees trade read amplification for enormous **write** throughput. The
core idea: **never update in place on disk; only append.**

### 7.1 Structure

```
 ┌──────────┐   flush when full   ┌────────────┐
 │ Memtable │  ─────────────────▶ │ Immutable  │
 │ (in RAM) │                     │  Memtable  │
 └──────────┘                     └─────┬──────┘
      ▲                                 │  write out as sorted file
      │ writes go here                  ▼
      │                           ┌────────────┐
      │                           │  SSTable   │  Level 0
      │                           └────────────┘
      │                                 │  compaction
      │                                 ▼
      │                           ┌────────────┐
      │                           │  SSTable   │  Level 1
      │                           └────────────┘
      │                                 ...
```

- **Memtable**: in-memory sorted structure (skip list or B-Tree). Writes
  go here first *and* to the WAL.
- When the memtable hits a size threshold, it's flushed to an **SSTable**
  (Sorted String Table): an immutable, sorted key→value file, typically
  with a block-based layout, a sparse index, and a Bloom filter.
- Background **compaction** merges SSTables from level `i` into level
  `i+1`, deduplicating keys and dropping tombstones.

### 7.2 Reads

To look up a key, check memtable → immutable memtable → each SSTable
from newest to oldest. Each SSTable's Bloom filter lets you skip it
cheaply if the key definitely isn't there.

### 7.3 Compaction strategies

- **Leveled** (LevelDB/RocksDB default): each level is ~10× bigger than
  the one above. Lower write amplification per level but more total work.
- **Tiered / size-tiered** (Cassandra): accumulate many SSTables of
  similar size; merge groups of them. Higher space amplification, lower
  write amp.

### 7.4 Tradeoffs vs. B+Tree

|                  | B+Tree              | LSM Tree            |
|------------------|---------------------|---------------------|
| Writes           | Random I/O, update in place | Sequential appends — fast |
| Reads            | 1 tree traversal    | Possibly many SSTables; Bloom filters help |
| Space amp        | Low                 | Higher (old versions until compacted) |
| Range scans      | Excellent (leaf links) | Good (merge across sorted runs) |
| Fragmentation    | Over time, yes      | Handled by compaction |

LSMs are why modern KV stores (RocksDB) eat write-heavy workloads.

---

## 8. Query processing

Turning a SQL string into an answer is a pipeline.

### 8.1 Parser

SQL text → **AST**. Handwritten recursive-descent parsers and generated
ones (e.g., `yacc`/`bison`, ANTLR) both work. Postgres uses a Bison
grammar. For your own DB, a handwritten Pratt parser is easy and fun.

### 8.2 Binder / analyzer

Resolve identifiers against the **catalog**:

- Does the table exist?
- Do the column names exist? What are their types?
- Implicit casts; function/operator resolution.
- Output a **logical plan** — a tree of relational-algebra-like operators
  (Scan, Select/Filter, Project, Join, Aggregate, Sort, Limit).

### 8.3 Rewriter

Applies algebraic rewrites that are always good:

- Push selections down past joins (filter early, join less).
- Push projections down.
- Constant folding (`WHERE 1=1` → true).
- Flatten subqueries into joins where possible.
- Handle views by substituting their definition.

### 8.4 Planner / optimizer

Picks the **physical plan**: which access method per table, which join
order, which join algorithm, whether to sort or hash. Huge topic — §9.

### 8.5 Execution engine

Three common execution models:

**(a) Iterator / Volcano model** — each operator implements
`open()/next()/close()`. `next()` returns one tuple. Composable, simple
to implement, but has per-tuple virtual-call overhead.

```
           ┌──────────┐
           │  Limit   │
           └────┬─────┘
                ▼ next()
           ┌──────────┐
           │   Sort   │
           └────┬─────┘
                ▼ next()
           ┌──────────┐
           │ HashJoin │────▶ next() from inner
           └────┬─────┘
                ▼ next() from outer
           ┌──────────┐
           │  Filter  │
           └────┬─────┘
                ▼
           ┌──────────┐
           │   Scan   │ (reads pages from buffer pool)
           └──────────┘
```

**(b) Vectorized / batch iterators** (MonetDB/X100, DuckDB, ClickHouse):
`next()` returns a *batch* of ~1024 tuples. Amortizes virtual calls,
keeps hot loops in L1 cache, enables SIMD.

**(c) Push-based / data-centric (code generation)** — HyPer, Umbra:
JIT-compile the whole plan into one tight loop. Fastest but hard to build.

**Core operators**:

- **SeqScan**: read all pages of a table.
- **IndexScan / IndexOnlyScan**: traverse an index for matching keys.
- **Filter (σ)**: evaluate predicate, drop non-matching.
- **Project (π)**: compute output columns.
- **Sort**: in-memory if small; external merge sort if bigger than RAM.
- **Aggregate**: either **HashAgg** (hash map keyed by GROUP BY cols) or
  **SortAgg** (sort then streaming group).
- **Join algorithms**:
  - **Nested Loop Join**: for each outer row, scan inner. `O(N*M)`. Fine
    when inner is tiny or indexed.
  - **Index Nested Loop Join**: uses an index on the inner side — great
    for selective joins.
  - **Sort-Merge Join**: sort both sides, then walk them like merge. Good
    when inputs are already sorted or very large.
  - **Hash Join**: build a hash table on the smaller side, probe with the
    larger. Usually the default for equijoins on big inputs. **Grace hash
    join** handles the case where the build side doesn't fit in RAM by
    partitioning both inputs to disk first.
- **Set ops** (`UNION`, `INTERSECT`): usually sort-based or hash-based.

**External sorting** (data bigger than RAM):
1. Read runs of size = memory, sort in RAM, write each sorted run to disk.
2. Merge `k` runs at a time (priority queue) into longer runs.
3. Final pass streams the answer.

---

## 9. Query optimization

The planner's job: among all *equivalent* physical plans, pick the one
whose estimated cost is lowest.

### 9.1 Rule-based (RBO)

Apply a fixed set of "always good" rules (the rewrites above). Cheap,
predictable, but can't choose between, say, hash join and merge join —
that's data-dependent.

### 9.2 Cost-based (CBO)

Estimate **cardinalities** and **costs** for each candidate plan.

**Statistics** the DB collects (usually via `ANALYZE`):

- Row count per table.
- Per-column: number of distinct values (NDV), nulls, min/max,
  most-common-values list, histograms (equi-width / equi-depth).
- Column correlation (some DBs).

**Cardinality estimation** — the hardest part:

- `σ (A = v)`: `rows / NDV(A)` if uniform; consult MCV list or histogram.
- `σ (A < v)`: use histogram.
- `A JOIN B ON A.x = B.y`: `|A| * |B| / max(NDV(A.x), NDV(B.y))`.
- Multiple predicates: independence assumption (`selectivity(p1 AND p2) ≈
  sel(p1) * sel(p2)`) — often wrong, a major source of bad plans.

**Cost model**: weighted mix of estimated disk pages read, CPU per tuple,
and maybe network. Postgres has a handful of tunable constants
(`seq_page_cost`, `random_page_cost`, `cpu_tuple_cost`).

### 9.3 Join order enumeration

For N tables, there are exponentially many join orderings and tree
shapes. Classic approaches:

- **System R dynamic programming** (Selinger et al., 1979) — considers
  all left-deep trees; still the basis of most optimizers. Exponential in
  N but fine up to ~10–15 tables.
- **Bushy DP** / DPccp — considers bushy trees too.
- **Randomized / genetic** (Postgres switches to `geqo` past 12 tables).
- **Top-down, memoized (Cascades / Volcano framework)** — used by SQL
  Server, CockroachDB, modern Greenplum/Orca; very extensible via
  transformation rules.

### 9.4 Why optimizers get it wrong

- Bad cardinality estimates compound across joins (error grows
  exponentially with join depth — Leis et al., "How Good Are Query
  Optimizers, Really?").
- Correlated predicates break the independence assumption.
- Stale stats.
- Parameter sniffing (plan picked for one parameter value, reused for a
  different one).

In practice, "adaptive" techniques (runtime re-planning, learned
cardinality models) are active research areas.

---

## 10. Transactions and ACID

A **transaction** is a unit of work — one or more reads/writes — that the
DB treats as a single logical operation with four guarantees:

- **A — Atomicity**: all or nothing. If any part fails, the whole txn is
  rolled back.
- **C — Consistency**: the DB moves from one valid state (constraints,
  FKs, uniqueness) to another. Mostly a property of the *schema*, not
  the engine.
- **I — Isolation**: concurrent txns behave as if they ran one at a time
  (in some order). In practice, weaker isolation levels are common —
  see below.
- **D — Durability**: once a txn commits, its effects survive crashes.

### 10.1 Isolation levels (SQL standard)

Defined by the anomalies they allow:

| Level              | Dirty read | Non-repeatable read | Phantom read |
|--------------------|:----------:|:-------------------:|:------------:|
| Read Uncommitted   | ✔︎ allowed | ✔︎                  | ✔︎           |
| Read Committed     |           | ✔︎                  | ✔︎           |
| Repeatable Read    |           |                    | ✔︎           |
| Serializable       |           |                    |             |

Real systems add non-standard levels, notably **Snapshot Isolation**
(Oracle's "serializable", Postgres's `REPEATABLE READ`, InnoDB default):
every txn reads from a consistent snapshot as of its start time. SI
prevents many anomalies but allows **write skew**. Postgres offers
**Serializable Snapshot Isolation (SSI)** (Cahill 2008), which adds SI +
runtime dependency tracking to detect and abort serializability
violations.

- **Dirty read**: seeing another txn's uncommitted write.
- **Non-repeatable read**: same query twice in one txn returns different
  rows because another txn updated them.
- **Phantom read**: same range query returns new rows because another
  txn inserted.
- **Write skew**: two txns each read a set, each write disjoint rows,
  both commit — final state violates an invariant neither txn would have
  violated alone.

### 10.2 The transaction manager

Maintains a table of active transactions with:

- **Transaction id** (TxID / XID) — monotonically increasing.
- Status: active / committed / aborted.
- Start and commit timestamps (for MVCC).
- List of locks held, or the read/write set.

---

## 11. Concurrency control

The engine's mechanism for giving you the isolation level you asked for.

### 11.1 Two-Phase Locking (2PL)

Each object has read (shared, S) and write (exclusive, X) locks. Txns
acquire locks before access. **2PL rule**: a txn first only *acquires*
locks (growing phase), then only *releases* (shrinking phase). It
produces conflict-serializable schedules.

**Strict 2PL** (SS2PL) holds all locks until commit/abort — this is what
most locking DBs actually do, because it also prevents cascading aborts.

Problems:
- **Deadlocks**: two txns each holding a lock the other needs. Handled
  by (a) a wait-for graph + periodic cycle detection + victim selection,
  or (b) timeouts, or (c) wound-wait / wait-die ordering by timestamp.

Granularity: row locks, page locks, table locks. Finer = more
concurrency but more lock-manager overhead. Most DBs support **lock
escalation** (many row locks → one table lock when a threshold is hit).

### 11.2 Timestamp ordering (T/O)

Every txn gets a timestamp at start; reads/writes are ordered by
timestamp. If a txn tries to read something written by a later txn, or
write something read by a later txn, abort.

### 11.3 Optimistic concurrency control (OCC)

Three phases: **Read** (do work on a local copy), **Validate** (at
commit, check no one wrote what you read), **Write** (apply). Good for
low-conflict workloads. Used by many in-memory DBs (Hekaton, Silo).

### 11.4 MVCC — Multi-Version Concurrency Control

The dominant approach in modern RDBMS (Postgres, Oracle, MySQL-InnoDB,
SQL Server snapshot mode, CockroachDB, Spanner).

Core idea: **writes create a new version of a row rather than
overwriting.** Each version is tagged with the creating txn's id. Readers
see the latest version visible to their snapshot *without blocking
writers*.

Two main flavors:

**(a) Append-only / version chain in heap (Postgres)**:
- `UPDATE` writes a new row, marks the old one with `xmax = <my XID>`.
- Each row has `(xmin, xmax)` — created by xmin, deleted by xmax.
- A transaction with snapshot `S` sees row iff `xmin` is committed ≤ `S`
  *and* (`xmax` is 0 or not committed ≤ `S`).
- Dead versions are cleaned up later by **VACUUM**.

**(b) Rollback segment / undo log (Oracle, InnoDB)**:
- Update *does* modify the heap in place, but writes the old image to an
  undo segment.
- A reader that needs an older version reconstructs it by walking undo.
- No vacuum, but undo can grow; long-running queries see
  "snapshot too old."

**Writer vs. writer** still needs locks (or OCC): two txns can't both
create the next version of the same row. Readers never block writers or
each other — that's MVCC's killer feature.

### 11.5 Phantoms and predicate locks

Standard row locks don't prevent *new* rows from appearing in a range.
Fixes:

- **Predicate locking** (theoretically clean, expensive in practice).
- **Index-range locks / next-key locks** (InnoDB): lock the gap between
  index keys so no one can insert into it.
- **SSI** (Postgres): detect the dangerous read/write dependency
  patterns and abort one txn.

---

## 12. Crash recovery and durability (WAL / ARIES)

The whole point: after a crash (power loss, OS panic, `kill -9`), bring
the DB back to a state where every committed txn's effects are present
and no aborted/in-flight txn's effects are visible.

### 12.1 Write-Ahead Logging (WAL) — the rule

> **Before any modified page is written to disk, the log records
> describing the modification must already be on disk.**

Consequence: at any moment, the log is at least as up-to-date as the
on-disk data pages. After a crash, we can recover by replaying the log.

### 12.2 Log records

A log is an append-only file of records. Each has:

- **LSN** (Log Sequence Number): monotonically increasing byte offset
  into the log.
- **TxID** that created it.
- **Type**: update / insert / delete / commit / abort / CLR / checkpoint.
- **prevLSN**: previous log record for the same txn (forms a per-txn
  linked list — lets you walk a txn's history backward for UNDO).
- **pageID**, **offset**, **before-image**, **after-image** (for
  physical logging) or a logical op description.

### 12.3 Physical vs. logical logging

- **Physical**: "at page P, offset O, bytes were X, now Y." Easy to
  replay idempotently.
- **Logical**: "insert row R into table T." Smaller, but replay must
  reach the same state even if the page layout has changed (hard).
- **Physiological** (ARIES): physical *within* a page, logical
  *across* pages. Sweet spot; what most systems use.

### 12.4 LSNs on pages

Each page carries the **pageLSN** = the LSN of the last log record that
modified it. Recovery uses this to decide: "is this change already on
this page?" If `pageLSN ≥ logRec.LSN`, skip; else apply.

### 12.5 Commit

To commit a txn:

1. Write a `COMMIT` log record.
2. **`fsync` the log up to that record** (this is the only truly
   synchronous disk write on the hot path).
3. Tell the client "committed."

Dirty data pages can stay in the buffer pool — they'll be written out
lazily, and if we crash first, recovery will redo them from the log.
That's the NO-FORCE side of STEAL + NO-FORCE.

### 12.6 Checkpoints

Replaying the log from the beginning every crash is impossibly slow, so
periodically we **checkpoint**: write a record that says "here's the
state of active txns and dirty pages at this moment." Recovery can
start from the last checkpoint rather than LSN 0.

**Fuzzy checkpoints** don't quiesce the system; they snapshot the state
(which dirty pages exist, which txns are active) and write a record,
while allowing normal work to continue. ARIES uses these.

### 12.7 ARIES recovery (the gold standard)

ARIES (Mohan et al., 1992) defines three passes on restart:

1. **Analysis pass**: scan log from last checkpoint forward; rebuild:
   - *Transaction Table* (active txns at crash) with their `lastLSN`.
   - *Dirty Page Table* (pages that may have had uncommitted changes),
     each with a `recLSN` (earliest LSN that dirtied it).

2. **Redo pass**: scan forward from the minimum `recLSN` in the Dirty
   Page Table. For each update log record on a page in the DPT, if
   `pageLSN < logRec.LSN`, re-apply the change. Redo is **idempotent
   and repeats history** — even undo actions of crashed txns get redone,
   because they were logged too.

3. **Undo pass**: for each txn in the Transaction Table that didn't
   commit, walk its log backwards via `prevLSN`, undoing each change and
   writing a **Compensation Log Record (CLR)** for it. CLRs are redo-only
   (they have an `undoNextLSN` pointing to the next record to undo),
   which means if we crash *again* during recovery, we don't redo the
   undo: we continue undoing from where the CLR points.

Key properties of ARIES:
- Works with STEAL + NO-FORCE buffer policies.
- Supports fine-grained (record-level) logging and locking.
- Repeats history (Redo) then undoes losers — clean and correct.

### 12.8 Group commit

Flushing the log on every commit is expensive. Let many txns' commit
records accumulate, then do one `fsync` — a huge throughput win for
small txns.

---

## 13. The client/server and wire protocol layer

Users usually talk to a DB over TCP. Each DB defines a **wire protocol**:

- Postgres: length-prefixed messages, text or binary encodings, a clean
  startup/auth/simple-query/extended-query flow.
- MySQL: similar idea, different bytes.
- Redis: RESP — trivial text-based framing.

A connection has state: authenticated user, current database, prepared
statements, transaction state, session settings.

**Prepared statements** cache the parsed plan: parse once, execute many
times with different parameter values. Saves parsing/planning cost.

Servers usually use one process or thread per connection (Postgres:
one process; MySQL: one thread) or an event loop with a thread pool
(modern designs). Connection pooling (client-side or via pgBouncer) is
the usual answer for thousands of clients.

---

## 14. Beyond a single node (brief)

You probably don't need these to start, but worth knowing they exist:

- **Replication**: primary writes, replicas follow. Either ship the WAL
  (physical replication) or ship logical row changes (logical
  replication). Trade-offs: synchronous (slow but no data loss on
  failover) vs. asynchronous (fast but possible lag/data loss).

- **Sharding / partitioning**: split data across nodes by key range or
  hash. Scales writes horizontally. Cross-shard transactions are the
  hard part.

- **Consensus** (Raft, Paxos): how a cluster agrees on the next log
  entry so the system stays consistent despite failures. Used in
  CockroachDB, Spanner, TiDB, etcd.

- **Distributed transactions**: 2PC (two-phase commit) for atomicity
  across shards; can block if the coordinator fails. Google Spanner
  uses TrueTime + 2PC + Paxos to get external consistency at global
  scale.

- **CAP / PACELC**: in a partition you must choose Consistency or
  Availability; absent partitions you still trade Latency vs.
  Consistency.

---

## 15. Suggested implementation roadmap

A sensible order to build things so each stage is testable on its own:

1. **Disk manager + page abstraction.** Fixed-size pages in a file;
   `read_page(id)` / `write_page(id, bytes)`. Done? You have a
   reliable block store.

2. **Buffer pool.** Frame array + page table + a simple replacement
   policy (LRU or clock). Exposes `pin/unpin/new_page/flush`. Verify
   with tests that evict and re-read.

3. **Slotted page + record format.** Implement `insert_record`,
   `get_record(rid)`, `delete_record(rid)`, `update_record(rid, bytes)`.
   Tests: fill a page, delete, compact, reinsert.

4. **Heap file / table.** A table is a linked list of pages, or a list
   tracked in a catalog page. Scans walk pages, iterate slots.
   `INSERT/SCAN/DELETE` by RID works now.

5. **Catalog.** A special "system" table describing user tables and
   columns. Bootstraps from page 0 of a well-known file.

6. **Write-Ahead Log.** Append-only file of records with LSNs. Page
   writes check "log is flushed up to `pageLSN`" before writing back.
   Commit does `fsync` on the log.

7. **Transactions + recovery.** Implement `BEGIN/COMMIT/ABORT`. On
   startup, do ARIES-style analysis/redo/undo. Verify by killing the
   process mid-insert and re-opening.

8. **B+Tree index.** Internal + leaf node pages; insert with split,
   delete with merge/borrow. Start single-threaded; add latch crabbing
   later. Add range scan via leaf links.

9. **SQL front-end (optional, or do a mini-language first).** Tokenizer
   → parser → AST for a subset (`CREATE TABLE`, `INSERT`, `SELECT … WHERE
   … ORDER BY`, `DELETE`, `UPDATE`). Hand-written Pratt parser is a
   pleasant exercise.

10. **Binder + logical plan + simple planner.** Pick SeqScan or
    IndexScan based on predicates. Emit a Volcano-style operator tree.

11. **Executor: SeqScan, Filter, Project, NestedLoopJoin, Sort,
    Limit.** Implement operators with `open/next/close`. Add `HashJoin`
    and `HashAgg` once the basics work.

12. **Concurrency control.** Start with a global table lock to prove
    correctness. Upgrade to row-level locking (2PL) or MVCC. MVCC is
    more code but reads are lock-free and the user experience is much
    better.

13. **Statistics + cost-based optimization.** `ANALYZE` populates
    per-column stats; planner picks join order and algorithms by cost.

14. **Network protocol.** Simplest is a line-based protocol ("send me
    SQL, I send back rows"). Later, implement the Postgres wire
    protocol if you want real client compatibility — `psql` and JDBC
    drivers just work.

Every stage produces something you can demo: a KV store after step 3,
a durable KV after step 7, a tiny SQL engine after step 11.

---

## 16. Further reading

**Books (in rough order of depth):**

- *Designing Data-Intensive Applications* — Martin Kleppmann. The best
  single overview of modern data systems.
- *Database Internals* — Alex Petrov. Exactly the book for this task:
  storage, B-trees, LSM, distributed.
- *Readings in Database Systems ("The Red Book")* — Hellerstein &
  Stonebraker. Curated classic papers with commentary. Free online.
- *Architecture of a Database System* — Hellerstein, Stonebraker, Hamilton
  (2007). A single long paper mapping out the whole DBMS. Free PDF.
- *Database Management Systems* — Ramakrishnan & Gehrke. Classic textbook.
- *Transaction Processing: Concepts and Techniques* — Gray & Reuter. The
  bible on transactions, logging, and recovery. Dense but definitive.

**Papers worth reading:**

- Selinger et al., *Access Path Selection in a Relational Database
  Management System* (1979) — System R optimizer.
- Lehman & Yao, *Efficient Locking for Concurrent Operations on
  B-Trees* (1981) — B-link trees.
- Haerder & Reuter, *Principles of Transaction-Oriented Database
  Recovery* (1983) — STEAL/NO-FORCE taxonomy.
- Mohan et al., *ARIES: A Transaction Recovery Method…* (1992).
- Stonebraker et al., *The End of an Architectural Era* (2007) — why
  rethink from scratch is sometimes worth it.
- O'Neil et al., *The Log-Structured Merge-Tree* (1996).
- Cahill, Röhm, Fekete, *Serializable Isolation for Snapshot Databases*
  (2008) — SSI.
- Leis et al., *How Good Are Query Optimizers, Really?* (2015).

**Courses (free, excellent):**

- **CMU 15-445 / 15-721** (Andy Pavlo). Videos on YouTube + the BusTub
  project. Hands-down the best intro-to-DB-internals course publicly
  available. The assignments have you build exactly the components
  above.
- **Berkeley CS186**.

**Reference implementations to read (pick one, don't drown):**

- **SQLite** — single-file codebase, well-commented, full SQL DB. The
  architectural doc `arch.html` on sqlite.org is short and excellent.
- **LevelDB** — ~20K lines of tidy C++; canonical LSM implementation.
- **BoltDB / etcd-io/bbolt** — single-file B+Tree KV store in Go. Tiny,
  beautiful, mmap-based.
- **BusTub** (CMU teaching DBMS) — C++ skeleton used in 15-445.
- **Postgres** — huge but extraordinarily well-documented; the `README`
  files in `src/backend/access/nbtree`, `src/backend/storage/buffer`,
  `src/backend/access/transam` are gold.

Read the SQLite architecture doc first. Then watch the first few CMU
15-445 lectures. Then pick one simple reference (BoltDB for B+Tree, or
LevelDB for LSM) and read its source while you start on step 1 of the
roadmap above.

Good luck. Building a database from scratch is one of the most
educational projects in all of software — by the end you will
understand computers at a depth most engineers never reach.
