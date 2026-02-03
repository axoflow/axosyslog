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

#include "filterx/expr-move.h"
#include "filterx/object-primitive.h"

typedef struct FilterXExprMove_
{
  FilterXFunction super;
  FilterXExpr *expr;
} FilterXExprMove;

static FilterXObject *
_eval_move(FilterXExpr *s)
{
  FilterXExprMove *self = (FilterXExprMove *) s;

  /* we could potentially delegate this operation to the expr and even to
   * the object, so we would not have to resolve the expression twice */

  return filterx_expr_move(self->expr);
}

static gboolean
_walk_children(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXExprMove *self = (FilterXExprMove *) s;

  return filterx_expr_visit(s, &self->expr, f, user_data);
}

static void
_free(FilterXExpr *s)
{
  FilterXExprMove *self = (FilterXExprMove *) s;

  filterx_expr_unref(self->expr);
  filterx_function_free_method(&self->super);
}

FilterXExpr *
filterx_function_move_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXExprMove *self = g_new0(FilterXExprMove, 1);
  filterx_function_init_instance(&self->super, "move", FXE_WRITE);

  self->super.super.eval = _eval_move;
  self->super.super.walk_children = _walk_children;
  self->super.super.free_fn = _free;

  if (filterx_function_args_len(args) == 1)
    {
      self->expr = filterx_function_args_get_expr(args, 0);
      if (!filterx_expr_move_available(self->expr))
        {
          g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                      "expected argument to be movable");
          goto error;
        }
    }
  if (!filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}
