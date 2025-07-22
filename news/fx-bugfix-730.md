`xml`: fixing a occasionally occurring crash in format_xml

In case the input was not a `dict`, a crash could occour during logging the error.
