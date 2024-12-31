
#ifndef SYSLOG_NG_PERF_H_INCLUDED
#define SYSLOG_NG_PERF_H_INCLUDED

#include "syslog-ng.h"

gpointer perf_generate_trampoline(gpointer target_address, const gchar *symbol_name);
gboolean perf_is_trampoline_address(gpointer address);


void perf_global_init(void);
void perf_global_deinit(void);


#endif
