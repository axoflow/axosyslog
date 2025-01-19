#ifndef FILTERX_SWITCH_H_INCLUDED
#define FILTERX_SWITCH_H_INCLUDED

#include "filterx-expr.h"


/* the switch statement */
FilterXExpr *filterx_switch_new(FilterXExpr *selector, GList *body);

/* a case in the switch statement */
FilterXExpr *filterx_switch_case_new(FilterXExpr *value);

FILTERX_EXPR_DECLARE_TYPE(switch);
FILTERX_EXPR_DECLARE_TYPE(switch_case);

#endif
