`s3`: retry part uploads on every network error

Only a refused connection was retried. A timeout or a closed connection during a part upload was
treated as an unexpected error, which left the part on disk until the next start and crashed the
destination on shutdown.
