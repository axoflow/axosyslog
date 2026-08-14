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

#include "filterx/filterx-ast.h"

static struct json_object *
_format_location(const CFG_LTYPE *lloc)
{
  struct json_object *location = json_object_new_object();

  json_object_object_add(location, "file", lloc->name ? json_object_new_string(lloc->name) : NULL);
  json_object_object_add(location, "line", json_object_new_int(lloc->first_line));
  json_object_object_add(location, "column", json_object_new_int(lloc->first_column));

  return location;
}

static gboolean
_add_child(FilterXExpr *parent, FilterXExpr **child, gpointer user_data)
{
  struct json_object *children = (struct json_object *) user_data;

  if (*child)
    json_object_array_add(children, filterx_expr_format_ast(*child));

  return TRUE;
}

static struct json_object *
_format_children(FilterXExpr *expr)
{
  struct json_object *children = json_object_new_array();

  filterx_expr_walk_children(expr, _add_child, children);

  return children;
}

struct json_object *
filterx_expr_format_ast(FilterXExpr *expr)
{
  if (!expr)
    return NULL;

  struct json_object *result = json_object_new_object();

  json_object_object_add(result, "type", expr->type ? json_object_new_string(expr->type) : NULL);
  json_object_object_add(result, "text", expr->expr_text ? json_object_new_string(expr->expr_text) : NULL);
  json_object_object_add(result, "location", expr->lloc ? _format_location(expr->lloc) : NULL);
  json_object_object_add(result, "children", _format_children(expr));

  return result;
}

GString *
filterx_expr_format_ast_string(FilterXExpr *expr)
{
  struct json_object *root = filterx_expr_format_ast(expr);
  GString *result = g_string_new(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY));

  json_object_put(root);

  return result;
}
