export abs_top_srcdir

EXTRA_DIST += \
	tests/light/CMakeLists.txt \
	tests/light/Makefile.am \
	tests/light/pytest.ini \
	tests/light/shared_files \
	tests/light/tox.ini

include tests/light/functional_tests/Makefile.am
include tests/light/axosyslog_light/Makefile.am


if HAVE_POETRY

PYTEST_SUBDIR=
PYTEST_VERBOSE=false
PYTEST_OPTS=

LIGHT_SRC_DIR=$(abs_top_srcdir)/tests/light
LIGHT_POETRY_CMD=$(POETRY) -C $(LIGHT_SRC_DIR)
LIGHT_PYTHON_CMD=$(LIGHT_POETRY_CMD) run python
LIGHT_PYTEST_CMD=$(LIGHT_PYTHON_CMD) -m pytest --showlocals --verbosity=3

light-venv:
	@$(LIGHT_POETRY_CMD) install

light-self-check pytest-self-check: light-venv
	@$(LIGHT_PYTEST_CMD) $(LIGHT_SRC_DIR)/axosyslog_light

light-check pytest-check: light-venv
	$(LIGHT_PYTEST_CMD) -o log_cli=$(PYTEST_VERBOSE) $(LIGHT_SRC_DIR)/functional_tests/$(PYTEST_SUBDIR) $(PYTEST_OPTS) --installdir=${prefix} --show-capture=no

light-linters pytest-linters: light-venv
	find $(abs_top_srcdir)/tests/light/ -name "*.py" \
		-not -path "*reports*" \
		-not -path "*tox*" \
		-print0 | xargs -0 \
		$(LIGHT_PYTHON_CMD) -m pre_commit run --show-diff-on-failure --config=$(LIGHT_SRC_DIR)/.pre-commit-config.yaml --files

else

light-venv light-self-check pytest-self-check light-check pytest-check light-linters pytest-linters:
	@echo "Poetry is missing, cannot execute E2E tests."
	@exit 1

endif


.PHONY: light-venv light-self-check light-check light-linters pytest-self-check pytest-check pytest-linters
