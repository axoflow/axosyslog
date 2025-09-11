`http()`: Added support for templated `headers()`

In case of batching the templates in `headers()` will be calculated
from the first message. Make sure to use `worker-partition-key()` to
group similar messages together.

Literal dollar signs (`$`) used in `headers()` must be escaped like `$$`.
