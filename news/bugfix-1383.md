`flags(destination-failover)`: Fixed threaded destinations (e.g. `http()`) counting as reachable again `time-reopen()`
after a failed send without any evidence, so new messages kept piling up for a dead node instead of failing over. A
threaded destination now stays unreachable until it delivers a batch again, or has nothing left to retry after a
dropped one.
