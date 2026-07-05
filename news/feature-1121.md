`filterx` error handling improvements: filterx errors were made more
concise.  Removed multiple layers of errors for simple statements, and
instead we use a single, but more precise error entry.

The single error entry points to the statement, instead of the
sub-expression, and the error message explains what failed.

Example, previously:

FILTERX ERROR; err_idx='[1/2]', expr='syslog-ng.conf:12:13|	$PID', error='Variable is unset: "$PID"'
FILTERX ERROR; err_idx='[2/2]', expr='syslog-ng.conf:12:4|	d[string($PID)]', error='Failed to get-subscript from object: Failed to evaluate key'

now:

FILTERX ERROR; err_idx='[1/1]', expr='syslog-ng.conf:12:4|	d[string($PID)]', error='Variable is unset: "$PID"'
