# axosyslog-light

**axosyslog-light** is AxoSyslog's lightweight end-to-end (E2E) test framework.
It is designed to facilitate the testing of AxoSyslog components and ensure their reliability and performance.

## Features

- **Configuration**: Easily customizable AxoSyslog configuration to suit various testing needs.
- **Lightweight**: Minimal dependencies and easy to set up.
- **E2E Testing**: Comprehensive end-to-end testing capabilities.
- **Integration with Poetry**: Simplified dependency management and packaging.
- **Support for Valgrind, GDB, and Strace**: Advanced debugging and profiling tools.

## Running 3rd party services in containers

Some tests need 3rd party services (`clickhouse-server`, `snmptrapd`). By default
(`--third-party-runner local`) these are expected to be installed on the host.

To run them in containers instead — so they don't need to be installed locally —
pass `--third-party-runner docker`:

```
make light-check PYTEST_OPTS="--third-party-runner docker"
```

This is independent of `--runner` (which selects how AxoSyslog itself runs), so a
locally installed AxoSyslog can be tested against containerized 3rd party services.

The images are hardcoded:
- ClickHouse: `clickhouse/clickhouse-server:latest` (pulled).
- snmptrapd: `axosyslog-light-snmptrapd:latest`, built from `tests/light/docker/snmptrapd.dockerfile`:

```
docker build -t axosyslog-light-snmptrapd:latest -f tests/light/docker/snmptrapd.dockerfile tests/light/docker
```

The containers run with `--network host`, so the services stay reachable on `localhost`
exactly as with local binaries.

## License

This project is licensed under the GNU General Public License version 3 or later.
