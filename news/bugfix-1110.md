`correlation`: fix radix parser end-of-input handling

Fixes two related radix matcher edge cases at end of input.

Parser scans now stop before '\0' to avoid reading past end-of-input and to keep captured lengths correct.
Parser-node traversal now continues with empty remaining input, so OPTIONALSET children can still match.
