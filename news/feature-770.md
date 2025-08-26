FilterX `parse_kv()`: add stray_words_append_to_value flag

For example,
```
filterx {
  $MSG = parse_kv($MSG, value_separator="=", pair_separator=" ", stray_words_append_to_value=true);
};


input: a=b b=c d e f=g
output: {"a":"b","b":"c d e","f":"g"}
```
