`date-parser()`, FilterX `strptime()`: Fixed an out-of-bounds read when deriving the month from an out-of-range
day of year. Such a day of year now rolls over into the previous or the following year.
