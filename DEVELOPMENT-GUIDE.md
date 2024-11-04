# Development guide

## Table of contents

1. [Source code structure](#source-code-structure)
2. [Architectural overview](#architectural-overview)
3. [Coding guidelines](#coding-guidelines)

## Source code structure

The code is organized into:
- Core library (`lib` directory).
- Plugin drivers (`modules` directory).

The configuration language is defined in `lib/cfg-grammar.y` and module related `...-grammar.ym` files.

Supported build systems are Autotools and CMake.

Testing:
- Unit tests use the [Criterion][ar:criterion] framework and can be found alongside its respective source code (`.../tests` directory).
- End-to-end tests are written in Python with [pytest][ar:pytest] (`tests/light` directory).

Packaging:
- Located in the `packaging` directory, including Alpine-based container images, Helm charts, and Debian/RPM packages.

## Architectural overview

### Main event loop

AxoSyslog uses [ivykis][ar:ivykis] for the main event loop.

### Log processing flow

Logs are received from local/remote `source`s, processed with transformations (`parser`s, `rewrite`s, `filter`s, or `filterx`), and routed to `destination`s.

`source`s can be:
- Non-blocking, socket-based (see `lib/logreader`).
- Blocking, threaded (see `lib/logthrsource`).

`destination`s can be:
- Non-blocking, socket-based (see `lib/logwriter`).
- Blocking, threaded (see `lib/logthrdest`).

## Coding guidelines

AxoSyslog is mainly written in C, with an object-oriented approach. Some modules are implemented in C++, Python or Java.

AxoSyslog follows clean coding principles:
- Functionality is layered in abstractions to enhance readability and maintainability.
- Function and variable names are descriptive to accurately communicate each layer's purpose.

### C code format

AxoSyslog follows GNU standards with additional conventions:
- Classes are in PascalCase, functions and variables in snake_case.
- Class namespaces are prefixed in function names.
- Private methods are `static` and start with an underscore (`_`).
- Function signatures are split into two lines (return type on the first, rest on the second) to help `git grep ^my_func_impl` searches.
- For more info, see [Patches](CONTRIBUTING.md#patches).

### Git history

- Each commit is atomic and detailed in the commit message.
- Commits represent the logical buildup of a feature, organized via interactive rebase to improve readability and maintainability.
- For more info see [Commits](CONTRIBUTING.md#commits).

### Comments

- Comments are rarely used, as clean coding and atomic commits are the main ways to document the code.
- Use comments where architectural overviews are needed or when interface contracts cannot be enforced.

### Code quality

AxoSyslog thrives for the highest quality, but supports developing features incrementally:
- Core abstractions or widely dependent code must be clean, optimized, and well-tested.
- Self-contained features may have relaxed standards but are iterated upon for improvement.

 [ar:criterion]: https://github.com/Snaipe/Criterion
 [ar:pytest]: https://github.com/pytest-dev/pytest
 [ar:ivykis]: https://github.com/buytenh/ivykis
