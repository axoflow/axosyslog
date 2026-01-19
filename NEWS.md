4.22.0
======

AxoSyslog is binary-compatible with syslog-ng [1] and serves as a drop-in replacement.

We provide [cloud-ready container images](https://github.com/axoflow/axosyslog/#container-images) and Helm charts.

Packages are available in our [APT](https://github.com/axoflow/axosyslog/#deb-packages) and [RPM](https://github.com/axoflow/axosyslog/#rpm-packages) repositories (Ubuntu, Debian, AlmaLinux, Fedora).

Check out the [AxoSyslog documentation](https://axoflow.com/docs/axosyslog-core/) for all the details.


## Features

  * FilterX `in` operator: Added support for `dict` keys for membership check.

    ```
    my_dict = {"foo": "foovalue", "bar": "barvalue"};
    my_needle = "foobar";

    if (my_needle in my_dict) {
      $MSG = "Found: " + my_dict[my_needle];
    } else {
      $MSG = "Not Found";
    };
    ```
    ([#888](https://github.com/axoflow/axosyslog/pull/888))


## Bugfixes

  * `parallelize()`: Fixed occasional crashes on high load.
    ([#904](https://github.com/axoflow/axosyslog/pull/904))

  * `parallelize()`: Fixed unoptimized parallelization with more `workers()` than CPU cores.
    ([#906](https://github.com/axoflow/axosyslog/pull/906))

  * FilterX `regexp_subst()` function: Fixed capture group references in the replacement argument.

    In the case of `global=true`, the value of capture group references were always used from
    that of the first match, e.g. the 2nd and subseqent matches used an incorrect value.
    ([#895](https://github.com/axoflow/axosyslog/pull/895))

  * `disk-buffer()`: Fixed various bugs.

    * Fixed potential writes beyond the configured front-cache limit.
    * Fixed a possible issue where memory usage metrics could reset on reload.

    ([#901](https://github.com/axoflow/axosyslog/pull/901))


## Other changes

  * `disk-buffer()`: Various smaller improvements and QoL features.

    * Added detailed debug logging for diskbuffer load and save operations,
      exposing the internal state of the non-reliable diskbuffer.
    * Improved front-cache balancing by enforcing the front-cache limit when
      distributing items between front-cache and front-cache-output.
    * Improved calculation of abandoned diskbuffer metrics by using existing
      file header data instead of loading the entire buffer file,
      significantly reducing reload time impact.

    ([#901](https://github.com/axoflow/axosyslog/pull/901))
    ([#902](https://github.com/axoflow/axosyslog/pull/902))


[1] syslog-ng is a trademark of One Identity.

## Discord

For a bit more interactive discussion, join our Discord server:

[![Axoflow Discord Server](https://discordapp.com/api/guilds/1082023686028148877/widget.png?style=banner2)](https://discord.gg/E65kP9aZGm)

## Credits

AxoSyslog is developed as a community project, and as such it relies
on volunteers, to do the work necessary to produce AxoSyslog.

Reporting bugs, testing changes, writing code or simply providing
feedback is an important contribution, so please if you are a user
of AxoSyslog, contribute.

We would like to thank the following people for their contribution:

Andras Mitzki, Attila Szakacs, Balazs Scheidler,
BenBryzak-brisbaneqldgovau, László Várady, Szilard Parrag, shifter
