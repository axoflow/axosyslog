set(METRICS_PROBE_FILTERX_SOURCES
  func-update-metric.c
  func-update-metric.h
)

add_module(
  TARGET metrics-probe-filterx
  SOURCES ${METRICS_PROBE_FILTERX_SOURCES}
  LIBRARY_TYPE STATIC
)
