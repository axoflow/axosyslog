/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "filterx/expr-template.h"
#include "filterx/object-message-value.h"
#include "filterx/filterx-eval.h"
#include "template/templates.h"
#include "scratch-buffers.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"

typedef struct _FilterXTemplate
{
  FilterXExpr super;
  LogTemplate *template;
} FilterXTemplate;

static FilterXObject *
_eval_template(FilterXExpr *s)
{
  FilterXTemplate *self = (FilterXTemplate *) s;
  FilterXEvalContext *context = filterx_eval_get_context();

  GString *value = scratch_buffers_alloc();
  LogMessageValueType t;

  /* FIXME: we could go directly to filterx_string_new() here to avoid a round trip in FilterXMessageValue */
  /* FIXME/2: let's make this handle literal and trivial templates */

  log_template_format_value_and_type_with_context(self->template, &context->msg, 1,
                                                  &context->template_eval_options, value, &t);

  /* NOTE: we borrow value->str here which is stored in a scratch buffer
   * that should be valid as long as we are traversing the filter
   * expressions, thus the FilterXObject is shorter lived than the scratch
   * buffer.  */

  return filterx_message_value_new_borrowed(value->str, value->len, t);
}

static void
_free(FilterXExpr *s)
{
  FilterXTemplate *self = (FilterXTemplate *) s;
  log_template_unref(self->template);
  filterx_expr_free_method(s);
}

/* NOTE: takes the object reference */
FilterXExpr *
filterx_template_new(LogTemplate *template)
{
  FilterXTemplate *self = g_new0(FilterXTemplate, 1);

  filterx_expr_init_instance(&self->super, "template", FALSE);
  self->super.eval = _eval_template;
  self->super.free_fn = _free;
  self->template = template;
  return &self->super;
}
