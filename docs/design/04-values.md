# Values and ordering

`Value` models NULL, BOOLEAN, INTEGER, REAL, TEXT, and BLOB with strict comparisons and numeric
promotion. `EncodeKey` uses a NULL marker, sign-flipped integer bits, IEEE-754 ordering, and
length-delimited text/blob groups; `DecodeKey` is fail-closed for malformed bytes.
