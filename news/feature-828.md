## clickhouse-destination: Added JSONCompactEachRow format and new `format` directive

This update enhances the ClickHouse destination by adding support for the `JSONCompactEachRow` format and introducing a new `format` directive for explicitly selecting the data format.

### Background
Previously, the destination supported:
- `Protobuf` (default when using `proto-var`)
- `JSONEachRow` (default when using `json-var`)

These defaults remain unchanged.

### What’s new
- Added support for `JSONCompactEachRow` — a more compact, array-based JSON representation (used with `json-var`).
- Introduced the `format` directive, allowing manual selection of the desired format:
  - `JSONEachRow`
  - `JSONCompactEachRow`
  - `Protobuf`

**Example:**
```hcl
destination {
  clickhouse (
    ...
    json-var(json("$my_filterx_json_var"))
    format("JSONCompactEachRow")
    ...
  );
};
```

**JSONEachRow (each JSON object per line, more readable):**

```
{"id":1,"name":"foo","value":42}
{"id":2,"name":"bar","value":17}
```

**JSONCompactEachRow (compact array-based row representation):**

```
[1,"foo",42]
[2,"bar",17]
```

### Validation and error handling

**Invalid format values now produce:**
```
Error parsing within destination: invalid format value 'invalid format', possible values:[JSONEachRow, JSONCompactEachRow, Protobuf]
```

**If the data’s actual format doesn’t match the selected format, ClickHouse returns:**
```
CANNOT_PARSE_INPUT_ASSERTION_FAILED
```
