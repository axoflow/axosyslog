/*
 * Copyright (c) 2026 Axoflow
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

#include "cfg-ast.h"
#include "cfg.h"
#include "logpipe.h"
#include "filterx/filterx-ast.h"
#include "filterx/filterx-pipe.h"
#include "compat/json.h"

static struct json_object *
_format_location(const gchar *filename, gint line, gint column)
{
  struct json_object *location = json_object_new_object();

  json_object_object_add(location, "file", filename ? json_object_new_string(filename) : NULL);
  json_object_object_add(location, "line", json_object_new_int(line));
  json_object_object_add(location, "column", json_object_new_int(column));

  return location;
}

static struct json_object *
_format_flags(guint32 flags)
{
  static const struct
  {
    guint32 flag;
    const gchar *name;
  } flag_names[] =
  {
    { LC_CATCHALL, "catch-all" },
    { LC_FALLBACK, "fallback" },
    { LC_FINAL, "final" },
    { LC_FLOW_CONTROL, "flow-control" },
    { LC_NO_FLOW_CONTROL, "no-flow-control" },
  };

  struct json_object *result = json_object_new_array();

  for (gsize i = 0; i < G_N_ELEMENTS(flag_names); i++)
    {
      if (flags & flag_names[i].flag)
        json_object_array_add(result, json_object_new_string(flag_names[i].name));
    }

  return result;
}

static struct json_object *_format_expr_node(LogExprNode *node);

static struct json_object *
_format_expr_node_list(LogExprNode *node)
{
  struct json_object *result = json_object_new_array();

  for (LogExprNode *child = node; child; child = child->next)
    json_object_array_add(result, _format_expr_node(child));

  return result;
}

static void
_add_pipe_members(struct json_object *result, LogPipe *pipe)
{
  json_object_object_add(result, "plugin", pipe->plugin_name ? json_object_new_string(pipe->plugin_name) : NULL);

  FilterXExpr *filterx_block = log_filterx_pipe_get_block(pipe);

  if (filterx_block)
    json_object_object_add(result, "filterx", filterx_expr_format_ast(filterx_block));
}

static struct json_object *
_format_expr_node(LogExprNode *node)
{
  struct json_object *result = json_object_new_object();

  json_object_object_add(result, "layout", json_object_new_string(log_expr_node_get_layout_name(node->layout)));
  json_object_object_add(result, "content", json_object_new_string(log_expr_node_get_content_name(node->content)));
  json_object_object_add(result, "name", node->name ? json_object_new_string(node->name) : NULL);
  json_object_object_add(result, "flags", _format_flags(node->flags));
  json_object_object_add(result, "location", _format_location(node->filename, node->line, node->column));

  if (node->content == ENC_PIPE && node->layout == ENL_SINGLE && node->object)
    _add_pipe_members(result, (LogPipe *) node->object);

  if (node->children)
    json_object_object_add(result, "children", _format_expr_node_list(node->children));

  return result;
}

static struct json_object *
_format_statement(LogExprNode *node)
{
  struct json_object *result = json_object_new_object();

  json_object_object_add(result, "kind", json_object_new_string(log_expr_node_get_content_name(node->content)));
  json_object_object_add(result, "expr", _format_expr_node(node));

  return result;
}

static gint
_compare_statements(gconstpointer a, gconstpointer b)
{
  const LogExprNode *n1 = *((LogExprNode **) a);
  const LogExprNode *n2 = *((LogExprNode **) b);

  gint result = g_strcmp0(n1->filename, n2->filename);

  if (result == 0)
    result = n1->line - n2->line;
  if (result == 0)
    result = n1->column - n2->column;

  return result;
}

static GPtrArray *
_collect_statements(CfgTree *tree)
{
  GPtrArray *statements = g_ptr_array_new();

  for (guint i = 0; i < tree->rules->len; i++)
    g_ptr_array_add(statements, g_ptr_array_index(tree->rules, i));

  GList *objects = cfg_tree_get_objects(tree);
  for (GList *object = objects; object; object = object->next)
    g_ptr_array_add(statements, object->data);
  g_list_free(objects);

  g_ptr_array_sort(statements, _compare_statements);

  return statements;
}

static struct json_object *
_format_statements(GlobalConfig *cfg)
{
  struct json_object *result = json_object_new_array();
  GPtrArray *statements = _collect_statements(&cfg->tree);

  for (guint i = 0; i < statements->len; i++)
    json_object_array_add(result, _format_statement(g_ptr_array_index(statements, i)));

  g_ptr_array_free(statements, TRUE);

  return result;
}

GString *
cfg_ast_format(GlobalConfig *cfg)
{
  struct json_object *root = json_object_new_object();

  json_object_object_add(root, "statements", _format_statements(cfg));

  GString *result = g_string_new(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY));
  json_object_put(root);

  return result;
}
