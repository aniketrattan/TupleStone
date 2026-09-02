# Ordered index facade

`BPlusTree` provides the stable insert/search/range/delete/validate contract and unique and
non-unique behaviour. The current implementation uses an ordered in-memory map behind the
contract; page-backed node splitting remains an intentional follow-up.
