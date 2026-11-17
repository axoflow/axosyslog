bin_PROGRAMS			+= filterx-tool/filterx-tool

filterx_tool_filterx_tool_SOURCES	= filterx-tool/filterx-tool.c

filterx_tool_filterx_tool_LDADD	= \
	$(MODULE_DEPS_LIBS) \
	$(TOOL_DEPS_LIBS)

EXTRA_DIST += filterx-tool/CMakeLists.txt

EXTRA_filterx_tool_filterx_tool_DEPENDENCIES	= lib/libsyslog-ng.la
