# Embedded API and CLI vertical slice

The current implementation wires the frozen public API to a durable, single-file table
store. Values are serialized field-by-field, writes replace the file through a temporary sibling,
and the public commit path records a checksummed snapshot WAL before replacing the data file. On
restart, only WAL snapshots with a matching commit record are replayed. Explicit transactions use
copy-on-write snapshots with generation checks for first-committer-wins conflict handling. The
parser supports the core DDL/DML/query subset, typed expressions, ordering, limits, distinct
results, and prepared `?` parameters.

The CLI is intentionally small and script-friendly. It accepts a database path, runs statements
from stdin, prints column headings and row values, and supports `.tables`, `.schema [TABLE]`,
`.timer on|off`, `.read FILE`, `.help`, and `.quit`. `tools/demo.ps1` exercises the public workflow.
TupleStone's public name and on-disk identity are now established together, so future renames
must be treated as compatibility changes.

Known follow-up work is tracked in `PLAN.md`: page-backed heap/index integration, full ARIES WAL
recovery, joins and grouped aggregates, external sorting, and the sanitizer/fuzz/crash harnesses.
