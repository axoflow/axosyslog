/*
 * Copyright (c) 2024 Axoflow
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

#include "filterx/filterx-expr.h"
#include "cfg-source.h"
#include "messages.h"
#include "mainloop.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"
#include "scratch-buffers.h"
#include "perf/perf.h"

void
filterx_expr_set_location_with_text(FilterXExpr *self, CFG_LTYPE *lloc, const gchar *text)
{
  if (lloc)
    {
      if (!self->lloc)
        self->lloc = g_new0(CFG_LTYPE, 1);
      *self->lloc = *lloc;
    }

  if (text && text != self->expr_text)
    {
      g_free(self->expr_text);
      self->expr_text = g_strdup(text);
    }
}

void
filterx_expr_set_location(FilterXExpr *self, CfgLexer *lexer, CFG_LTYPE *lloc)
{
  if (!self->lloc)
    self->lloc = g_new0(CFG_LTYPE, 1);
  *self->lloc = *lloc;

  g_free(self->expr_text);
  GString *res = g_string_sized_new(0);
  cfg_source_extract_token_text(lexer, lloc, res);
  self->expr_text = g_string_free(res, FALSE);
}

EVTTAG *
filterx_expr_format_location_tag(FilterXExpr *self)
{
  if (self && self->lloc)
    return evt_tag_printf("expr", "%s:%d:%d|\t%s",
                          self->lloc->name, self->lloc->first_line, self->lloc->first_column,
                          self->expr_text ? : "n/a");
  else
    return evt_tag_str("expr", "n/a");
}

GString *
filterx_expr_format_location(FilterXExpr *self)
{
  GString *location = scratch_buffers_alloc();

  if (self && self->lloc)
    g_string_printf(location, "%s:%d:%d", self->lloc->name, self->lloc->first_line, self->lloc->first_column);
  else
    g_string_assign(location, "n/a");

  return location;
}

const gchar *
filterx_expr_get_text(FilterXExpr *self)
{
  if (self && self->expr_text)
    return self->expr_text;

  return "n/a";
}

FilterXExpr *
filterx_expr_optimize(FilterXExpr *self)
{
  if (!self)
    return NULL;

  if (self->optimized)
    return self;

  self->optimized = TRUE;
  g_assert(!self->inited);

  if (!self->optimize)
    return self;

  FilterXExpr *optimized = self->optimize(self);
  if (!optimized)
    return self;

  /* the new expression may be also be optimized */
  optimized = filterx_expr_optimize(optimized);

  msg_trace("FilterX: expression optimized",
            filterx_expr_format_location_tag(self),
            evt_tag_str("old_type", self->type),
            evt_tag_str("new_type", optimized->type));
  if (self->lloc)
    {
      /* copy location information to the optimized representation */
      filterx_expr_set_location_with_text(optimized, self->lloc, self->expr_text);
    }
  /* consume original expression */
  filterx_expr_unref(self);
  return optimized;
}

static void
_init_sc_key_name(FilterXExpr *self, gchar *buf, gsize buf_len)
{
  g_snprintf(buf, buf_len, "fx_%s_evals_total", self->type);
}

gboolean
filterx_expr_init_method(FilterXExpr *self, GlobalConfig *cfg)
{
  gchar buf[256];

  _init_sc_key_name(self, buf, sizeof(buf));
  stats_lock();
  StatsClusterKey sc_key;
  StatsClusterLabel labels[1];
  gint labels_len = 0;

  if (self->name)
    labels[labels_len++] = stats_cluster_label("name", self->name);
  stats_cluster_single_key_set(&sc_key, buf, labels, labels_len);
  stats_register_counter(STATS_LEVEL3, &sc_key, SC_TYPE_SINGLE_VALUE, &self->eval_count);
  stats_unlock();

  if (perf_is_enabled())
    {
      if (self->expr_text)
        g_snprintf(buf, sizeof(buf), "filterx::%s", self->expr_text);
      else
        g_snprintf(buf, sizeof(buf), "filterx::@%s", self->type);
      self->eval = perf_generate_trampoline(self->eval, buf);
    }

  return TRUE;
}

void
filterx_expr_deinit_method(FilterXExpr *self, GlobalConfig *cfg)
{
  gchar buf[64];

  _init_sc_key_name(self, buf, sizeof(buf));
  stats_lock();
  StatsClusterKey sc_key;
  StatsClusterLabel labels[1];
  gint labels_len = 0;

  if (self->name)
    labels[labels_len++] = stats_cluster_label("name", self->name);
  stats_cluster_single_key_set(&sc_key, buf, labels, labels_len);
  stats_unregister_counter(&sc_key, SC_TYPE_SINGLE_VALUE, &self->eval_count);
  stats_unlock();
}

static gboolean
_init_child_exprs(FilterXExpr **expr, gpointer user_data)
{
  GlobalConfig *cfg = (GlobalConfig *) user_data;

  return filterx_expr_init(*expr, cfg);
}

static gboolean
_deinit_child_exprs(FilterXExpr **expr, gpointer user_data)
{
  GlobalConfig *cfg = (GlobalConfig *) user_data;

  filterx_expr_deinit(*expr, cfg);
  return TRUE;
}

gboolean
filterx_expr_init(FilterXExpr *self, GlobalConfig *cfg)
{
  if (!self || self->inited)
    return TRUE;
    
  /* init children first */
  if (!filterx_expr_walk_children(self, _init_child_exprs, cfg))
    goto failure;

  /* init @self */
  if (!self->init(self, cfg))
    goto failure;

  self->inited = TRUE;
  return TRUE;

failure:
  if (!filterx_expr_walk_children(self, _deinit_child_exprs, cfg))
    g_assert_not_reached();
  return FALSE;

}

void
filterx_expr_deinit(FilterXExpr *self, GlobalConfig *cfg)
{
  if (!self || !self->inited)
    return;

  if (self->deinit)
    self->deinit(self, cfg);

  if (!filterx_expr_walk_children(self, _deinit_child_exprs, cfg))
    g_assert_not_reached();

  self->inited = FALSE;
}

void
filterx_expr_free_method(FilterXExpr *self)
{
  g_free(self->lloc);
  g_free(self->expr_text);
}

void
filterx_expr_init_instance(FilterXExpr *self, const gchar *type)
{
  self->ref_cnt = 1;
  self->init = filterx_expr_init_method;
  self->deinit = filterx_expr_deinit_method;
  self->free_fn = filterx_expr_free_method;
  self->type = type;
}

FilterXExpr *
filterx_expr_new(void)
{
  FilterXExpr *self = g_new0(FilterXExpr, 1);
  filterx_expr_init_instance(self, "expr");
  return self;
}

FilterXExpr *
filterx_expr_ref(FilterXExpr *self)
{
  main_loop_assert_main_thread();

  if (!self)
    return NULL;

  self->ref_cnt++;
  return self;
}

void
filterx_expr_unref(FilterXExpr *self)
{
  main_loop_assert_main_thread();

  if (!self)
    return;

  if (--self->ref_cnt == 0)
    {
      g_assert(!self->inited);
      self->free_fn(self);
      g_free(self);
    }
}

FilterXExpr *
filterx_unary_op_optimize_method(FilterXExpr *s)
{
  FilterXUnaryOp *self = (FilterXUnaryOp *) s;

  self->operand = filterx_expr_optimize(self->operand);
  return NULL;
}

void
filterx_unary_op_free_method(FilterXExpr *s)
{
  FilterXUnaryOp *self = (FilterXUnaryOp *) s;

  filterx_expr_unref(self->operand);
  filterx_expr_free_method(s);
}

static gboolean
filterx_unary_op_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXUnaryOp *self = (FilterXUnaryOp *) s;

  return filterx_expr_visit(&self->operand, f, user_data);
}

void
filterx_unary_op_init_instance(FilterXUnaryOp *self, const gchar *name, FilterXExpr *operand)
{
  filterx_expr_init_instance(&self->super, name);
  self->super.optimize = filterx_unary_op_optimize_method;
  self->super.walk_children = filterx_unary_op_walk;
  self->super.free_fn = filterx_unary_op_free_method;
  self->operand = operand;
}

void
filterx_binary_op_free_method(FilterXExpr *s)
{
  FilterXBinaryOp *self = (FilterXBinaryOp *) s;

  filterx_expr_unref(self->lhs);
  filterx_expr_unref(self->rhs);
  filterx_expr_free_method(s);
}

FilterXExpr *
filterx_binary_op_optimize_method(FilterXExpr *s)
{
  FilterXBinaryOp *self = (FilterXBinaryOp *) s;

  self->lhs = filterx_expr_optimize(self->lhs);
  self->rhs = filterx_expr_optimize(self->rhs);
  return NULL;
}

static gboolean
filterx_binary_op_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXBinaryOp *self = (FilterXBinaryOp *) s;

  if (!filterx_expr_visit(&self->lhs, f, user_data))
    return FALSE;

  if (!filterx_expr_visit(&self->rhs, f, user_data))
    return FALSE;

  return TRUE;
}

void
filterx_binary_op_init_instance(FilterXBinaryOp *self, const gchar *name, FilterXExpr *lhs, FilterXExpr *rhs)
{
  filterx_expr_init_instance(&self->super, name);
  self->super.optimize = filterx_binary_op_optimize_method;
  self->super.walk_children = filterx_binary_op_walk;
  self->super.free_fn = filterx_binary_op_free_method;
  g_assert(lhs);
  g_assert(rhs);
  self->lhs = lhs;
  self->rhs = rhs;
}
