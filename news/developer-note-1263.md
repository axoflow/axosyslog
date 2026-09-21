`template`: renamed `log_template_is_message_independent()` to `log_template_is_fixed()`

A fixed template renders the same output for every message, and the new
`log_template_format_fixed()` formats one without taking a `LogMessage`.

The `http()` load balancer predicates follow the same polarity:
`http_lb_target_is_url_fixed()` and `http_load_balancer_is_url_fixed()`
replace their message-dependence counterparts.
