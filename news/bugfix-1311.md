`config`: Fixed a crash when a syntax error in an in-memory configuration snippet, such as a `$(filter ...)`
template argument or a patterndb action condition, made the parser read past the end of the snippet.
