# Benchmark notes

`tuplestone_bench` is a small, dependency-free smoke benchmark over the public API. It inserts a
deterministic workload inside one transaction, commits it through the WAL, then runs a filtered
`COUNT(*)` query. It is intended to make performance discussion reproducible, not to replace a
full benchmark suite.

Run:

```powershell
build\release\bin\tuplestone_bench.exe bench.db 20000
```

The executable prints the row count, matched rows, insert/query timings, and insert rows/sec. Do
not compare numbers across machines without recording compiler, build type, filesystem, and
`Options::sync_on_commit`; the default is deliberately durable and therefore slower than an unsafe
benchmark configuration.

Example capture (Windows UCRT64, RelWithDebInfo, 5,000 rows, local workspace):

```text
rows=5000 matched=2500 insert_us=149248 query_us=2970 insert_rows_per_sec=33501.3
```

The next performance milestone is a page-backed B+Tree and heap benchmark. Until then, these
numbers characterize the current compact snapshot engine only.
