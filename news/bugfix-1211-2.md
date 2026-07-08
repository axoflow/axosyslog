`s3`: retry failed part uploads instead of dropping the data

A part upload that failed with any client error previously discarded the buffered messages of the
failing part. It now follows the same policy as the `http()` destination: only responses that can never
succeed as-is are dropped, every other error including access denied and throttling keeps the buffered
data and the upload is retried a limited number of times, so a transient outage or a fixable
misconfiguration no longer loses messages.
