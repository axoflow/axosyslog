/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */


#include "filterx/expr-break.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-primitive.h"

typedef struct _FilterXExprBreak
{
  FilterXExpr super;
  gboolean in_switch;
} FilterXExprBreak;

static FilterXObject *
_eval_break(FilterXExpr *s)
{
  FilterXEvalContext *context = filterx_eval_get_context();
  context->eval_control_modifier = FXC_BREAK;

  return filterx_boolean_new(TRUE);
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXExprBreak *self = (FilterXExprBreak *) s;

  if (!self->in_switch)
    {
      filterx_eval_push_error_static_info("Failed to initialize break", "break statement not within a switch");
      return FALSE;
    }

  return filterx_expr_init_method(s, cfg);
}

static gboolean
_break_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  return TRUE;
}

void
filterx_expr_break_set_in_switch(FilterXExpr *s)
{
  FilterXExprBreak *self = (FilterXExprBreak *) s;

  self->in_switch = TRUE;
}

FilterXExpr *
filterx_expr_break(void)
{
  FilterXExprBreak *self = g_new0(FilterXExprBreak, 1);
  filterx_expr_init_instance(&self->super, FILTERX_EXPR_TYPE_NAME(break), FXE_CONTROL);
  self->super.init = _init;
  self->super.walk_children = _break_walk;
  self->super.eval = _eval_break;
#if SYSLOG_NG_ENABLE_JIT
  self->super.infer_types = filterx_expr_infer_types_boolean_result;
#endif

  return &self->super;
}

FILTERX_EXPR_DEFINE_TYPE(break);
