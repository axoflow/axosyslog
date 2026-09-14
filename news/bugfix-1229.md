`strptime()`, `date-parser()`: Fixed a field leak between the formats of a multi-format parse.

A format that failed midway left the fields it already parsed (for example the year) behind,
and a later matching format that does not name those fields kept them in the result.
