`http()` destination: Fixed templated `headers()` when batching is enabled.

Request headers were formatted from the first message of the batch, so a batch that mixed messages with different
header values was sent with the values of the first message.

Batching with a templated `headers()` now requires `worker-partition-key()` and flushes the batch when the key
changes, the same as a templated `url()` or `body-prefix()`.
