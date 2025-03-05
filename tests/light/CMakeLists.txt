find_package(Poetry)

set(LIGHT_SOURCE_DIR ${PROJECT_SOURCE_DIR}/tests/light)
set(LIGHT_POETRY_CMD ${Poetry_EXECUTABLE} -C ${LIGHT_SOURCE_DIR})
set(LIGHT_PYTHON_CMD ${LIGHT_POETRY_CMD} run python)
set(LIGHT_PYTEST_CMD ${LIGHT_PYTHON_CMD} -m pytest --showlocals --verbosity=3)

add_custom_target(light-self-check
   COMMAND ${LIGHT_POETRY_CMD} install
   COMMAND ${LIGHT_PYTEST_CMD} ${LIGHT_SOURCE_DIR}/axosyslog_light)

add_custom_target(light-check
   COMMAND ${LIGHT_POETRY_CMD} install
   COMMAND ${LIGHT_PYTEST_CMD} ${LIGHT_SOURCE_DIR}/functional_tests --installdir=${CMAKE_INSTALL_PREFIX} $$EXTRA_ARGS)

add_custom_target(light-linters
   COMMAND ${LIGHT_POETRY_CMD} install
   COMMAND find ${LIGHT_SOURCE_DIR} -name "*.py" -not -path "*reports*" -not -path "*tox*" -print0 | xargs -0 ${LIGHT_PYTHON_CMD} -m pre_commit run --show-diff-on-failure --config=${LIGHT_SOURCE_DIR}/.pre-commit-config.yaml --files)
