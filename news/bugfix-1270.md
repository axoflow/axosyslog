`opentelemetry()`: fixed the text form of a large integral double in `mode(filterx-dict)`. An OTLP timestamp in ticks
or in nanoseconds, sent as a double, printed in scientific notation, for example `6.3923074857349325e+17`. It now prints
as `639230748573493248.0`.
