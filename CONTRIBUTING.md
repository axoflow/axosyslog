# Contributing to AxoSyslog

AxoSyslog is developed as a community project, and relies on
volunteers to produce AxoSyslog. Reporting bugs, testing changes,
writing code, or simply providing feedback are all important
contributions. This guide provides guidelines to make contributing
easier.

For brief development guidelines and architecture overview, see
[Development Guide](DEVELOPMENT-GUIDE.md).

## Table of contents

1. [Issues](#issues)
    1. [Reporting bugs](#reporting-bugs)
    2. [Feature requests](#feature-requests)
    3. [Testing](#testing)
2. [Pull requests](#pull-requests)
3. [Additional resources](#additional-resources)

## Issues

One of the easiest ways to contribute to the development of AxoSyslog
is to participate in the discussions about features, bugs and design.
Some of these discussions started on the
[mailing list][ar:mailing-list], some in the
[issue tracker][ar:issue-tracker]. Bugs tagged
[`good first issue`][ar:issues:good-first-issue] are generally good targets to contribute your
feedback - but pretty much any open issue can be a good start!

### Reporting bugs

When you report a bug, it is important to share as much relevant
information as you can, including:
 * version number of AxoSyslog used;
 * the platform (operating system and its version, architecture, etc);
 * a backtrace from the core file if the issue is a crash (this can be
invaluable);
 * if possible, a configuration that triggers the problem;
 * a detailed description of the issue.

To make it easy to read reports, if you send a configuration snippet,
or a backtrace, use
[fenced code blocks](https://help.github.com/articles/github-flavored-markdown#fenced-code-blocks)
around them.

### Feature requests

We use the same [issue tracker][ar:issue-tracker] to handle features
requests (they're all tagged with the
[`enhancement`](https://github.com/axoflow/axosyslog/labels/enhancement)
label. You are welcome to share your ideas on existing requests, or to
submit your own.

### Testing

An incredibly useful way to contribute is to test patches and pull
requests - there's only so much [automated testing][ar:github-actions] can do.
For example, you can help testing on platforms the developers do not
have access to, or try configurations not thought of before.

## Pull requests

If you plan to open a pull request, please follow the guideline below.

### Branch

You need to fork AxoSyslog on GitHub, and create a working branch from main in your fork.

After writing the code, just before opening the pull request,
make sure your working branch is rebased against main.

### PR description
In the description, please explain (if applicable):
- The problem your pull request intends to solve.
- A general overview about the implementation, or any information that can help
reviewers to understand your code.
- Please explain how one can try out your code: configuration snippets,
or deployment information.
- You can mention if you were considering alternative solutions or explain
problems you ran into.
- If you submit a pull request that fixes an existing issue, please mention
the issue somewhere in the pull request, so we can close the
original issue as well.


The documentation is created from the description. Please provide
a description that can be a good input for the admin guide as well.

### Commits
#### Commit messages

```
The commit messages be formatted according to this:

module: short description

Long description, that may be
formatted in markdown.

Signed-off-by: your name <youremail@address.com>
```

This format is checked by the CI.

`module` refers to the part of AxoSyslog that the patch intends to change. For example
python, redis, persist, scratch-buffers, etc.

By signing off your commits (`--signoff`), you certify that you agree to the terms outlined in the
Developer Certificate of Origin (DCO), as detailed in [DCO](DCO.md).

If you do not want to share your email address due to privacy reasons,
you can use `some-id+yourgithubusername@users.noreply.github.com`,
which is automatically generated and tracked by github. You can check the exact
address in your github settings->email->primary email address, if you enabled email privacy.

#### Patches

We are using a coding style very similar to
[GNU Coding Standards](https://www.gnu.org/prep/standards/standards.html#Writing-C)
for AxoSyslog.

You can use `make style-check` or `make style-format` to check or format automatically your code.
These commands are executed by our CI as well.

If possible, please organize your code into a set of small and self contained patches,
with clear descriptions each, that can help to understand the patch. This greatly helps the review process.

Please follow clean code guidelines whenever possible. Functions should be small and responsible for one thing.
Try to avoid code duplication. Add descriptive names for functions and variables. Etc...

#### News file

We are automatically generating the news file before each release. So that to work,
please add a news entry for your change under the news directory.

The file name should be news/type-PR_ID.md (for example: news/bugfix-1234.md).
For now the following types are supported:
feature, bugfix, packaging, developer-note, other.

If you think there is no need for a news file (for example it is a small
fix for an earlier pull request of the same release),
you can leave a note about it in the pull request description.

The generated news file will contain a link to the pull request, where the interested
users can find detailed information in the description.
This means your news entry does not need to be too descriptive.

The news file format should look like this:

```
`module`: short description

long description
```

See news/README.md for more information.

### Testing

If possible please add tests for your change. You can add unit tests in c (tests directory in most of AxoSyslog modules),
or there is an initiative so that contributors write tests in python (for now the feature set is limited).
You can check `tests/light/functional_tests/source_drivers/generator_source/` as an example.

### CI

After opening the pull requests, one of the maintainers will enable
the tests to run for your pull requests. So that the pull request
could be merged, all tests must pass, and the PR needs two approvals from the maintainers.

You do not need to find reviewers. The maintainers continuously monitor the project,
and will assign themselves. We try to add feedback as soon as possible.

If you get stuck with a regression test, feel free to ask for help. Sometimes it is difficult to understand the test logs.


## Licensing

Please ensure that your contribution is clean in respect to licensing and
copyright.

If your contribution is eligible for copyright, you should also extend the list
of copyright holders at the top of the relevant files which carry your
modifications.

The absolute minimum to specify is the identity of the author entity, which is
usually one or more of an e-mail address and your full name or the name of the
legal entity who holds the intellectual rights if it is not you.
Please make it clear which is the case, because this may depend on your
contract if you are employed or are a subcontractor.

Note that from time to time, we may rephrase the exact text surrounding
attributions, however the specified identities and the license binding a given
contribution will not be changed in a legally incompatibly manner.

Every new file must carry a standard copyright notice and be compatible with
our licensing scheme described in COPYING.
You should observe some of the existing files for reference.

## Additional resources

To contact us, visit our [Discord server][ar:discord] where you can
ask questions, and discuss your feature requests with a wider
audience.

We use [GitHub issues][ar:issue-tracker] to track issues, feature requests
and patches. We are also using [GitHub Actions][ar:github-actions] for automatic
testing.

 [ar:discord]: https://discord.gg/E65kP9aZGm
 [ar:issue-tracker]: https://github.com/axoflow/axosyslog/issues
 [ar:issues:good-first-issue]: https://github.com/axoflow/axosyslog/labels/good%20first%20issue
 [ar:github-actions]: https://github.com/axoflow/axosyslog/actions
