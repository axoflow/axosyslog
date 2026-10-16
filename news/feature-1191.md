`filterx-tool`: added an experimental command line tool for working with FilterX code interactively

Currently, only the `script` subcommand is implemented:
`filterx-tool script --syntax-only <file>` parses a file as FilterX code and reports whether it is valid
`filterx-tool script --print-ast <file>` prints the abstract syntax tree of the parsed code as JSON
