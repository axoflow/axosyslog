/*
 * Copyright (c) 2025-2026 László Várady
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

#include "filterx/filterx-expr.h"
#include "filterx/filterx-object.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-primitive.h"

#if SYSLOG_NG_ENABLE_JIT

/* Attribute template function to determine which attributes JIT blocks need to have to be compatible for inlining. */
extern FilterXObject *fx_jit_attribute_template_opaque(FilterXEvalContext *ctx);

__attribute__((used))
FilterXObject *
fx_jit_attribute_template(FilterXEvalContext *ctx)
{
  return fx_jit_attribute_template_opaque(ctx);
}

/* De-inlined version of FilterX functions */
/* TODO: remove these once FilterX JIT is stable: non-inline calls should replace the inlined versions */

__attribute__((used))
FilterXObject *
fx_jit_expr_eval(FilterXExpr *self)
{
  return filterx_expr_eval(self);
}

__attribute__((used)) __attribute__((noinline))
gint32
fx_jit_expr_eval_stmt(FilterXExpr *self, FilterXEvalContext *context, FilterXObject **last_result)
{
  return fx_jit_process_expr_result(filterx_expr_eval(self), self, context, last_result);
}

__attribute__((used))
FilterXObject *
fx_jit_expr_eval_typed(FilterXExpr *self)
{
  return filterx_expr_eval_typed(self);
}

__attribute__((used))
FilterXObject *
fx_jit_expr_make_typed_object(FilterXExpr *self, FilterXObject *obj)
{
  return filterx_expr_make_typed_object(self, obj);
}

__attribute__((used))
FilterXObject *
fx_jit_object_ref(FilterXObject *self)
{
  return filterx_object_ref(self);
}

__attribute__((used))
FilterXObject *
fx_jit_object_cow_fork2(FilterXObject *self)
{
  if (!self)
    return NULL;
  return filterx_object_cow_fork2(self, NULL);
}

__attribute__((used))
void
fx_jit_object_unref(FilterXObject *self)
{
  filterx_object_unref(self);
}

__attribute__((used))
gboolean
fx_jit_object_truthy(FilterXObject *self)
{
  return filterx_object_truthy(self);
}

__attribute__((used))
FilterXObject *
fx_jit_boolean_new(gboolean value)
{
  return filterx_boolean_new(value);
}

#endif
