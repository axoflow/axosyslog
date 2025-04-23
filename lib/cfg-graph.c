/*
 * Copyright (c) 2019 Balabit
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "cfg-graph.h"
#include "logpipe.h"

typedef struct
{
  LogPipe *from;
  LogPipe *to;
  LogPathConnectionType type;
} Arc;

Arc *
arc_new(LogPipe *from, LogPipe *to, LogPathConnectionType type)
{
  Arc *self = g_new0(Arc, 1);
  self->from = from;
  self->to = to;
  self->type = type;

  return self;
};

void
arc_free(Arc *self)
{
  g_free(self);
}

static guint
arc_hash(Arc *arc)
{
  return g_direct_hash(arc->from);
}

static gboolean
arc_equal(Arc *arc1, Arc *arc2)
{
  return arc1->to == arc2->to;
}

static void
_append_arc(Arc *self, gpointer dummy, GPtrArray *arcs)
{
  g_ptr_array_add(arcs, g_strdup_printf("{\"from\" : \"%p\", \"to\" : \"%p\", \"type\" : \"%s\"}",
                                        self->from, self->to,
                                        self->type == PIW_NEXT_HOP ? "next_hop" : "pipe_next"));
}

static gchar *
arcs_as_json(GHashTable *arcs)
{
  GPtrArray *arcs_list = g_ptr_array_new_with_free_func(g_free);
  g_hash_table_foreach(arcs, (GHFunc)_append_arc, arcs_list);
  g_ptr_array_add(arcs_list, NULL);

  gchar *arcs_joined = g_strjoinv(", ", (gchar **)arcs_list->pdata);
  g_ptr_array_free(arcs_list, TRUE);

  gchar *json = g_strdup_printf("[%s]", arcs_joined);
  g_free(arcs_joined);

  return json;
}

static GList *
_collect_info(LogPipe *self)
{
  GList *info = g_list_copy_deep(self->info, (GCopyFunc)g_strdup, NULL);

  if (self->plugin_name)
    info = g_list_append(info, g_strdup(self->plugin_name));

  if (self->expr_node)
    {
      gchar buf[128];
      log_expr_node_format_location(self->expr_node, buf, sizeof(buf));
      info = g_list_append(info, g_strdup(buf));
    }

  if (log_pipe_get_persist_name(self))
    info = g_list_append(info, g_strdup(log_pipe_get_persist_name(self)));

  return info;
}

static gchar *
g_str_join_list(GList *self, gchar *separator)
{
  if (!self)
    return g_strdup("");

  if (g_list_length(self) == 1)
    return g_strdup(self->data);

  GString *joined = g_string_new(self->data);
  GList *rest = self->next;

  for (GList *e = rest; e; e = e->next)
    {
      g_string_append(joined, separator);
      g_string_append(joined, e->data);
    }

  return g_string_free(joined, FALSE);
}

static gchar *
_add_quotes(gchar *self)
{
  return g_strdup_printf("\"%s\"", self);
}

static void
_append_node(LogPipe *self, gpointer dummy, GPtrArray *nodes)
{
  GList *raw_info = _collect_info(self);
  GList *info_with_quotes = g_list_copy_deep(raw_info, (GCopyFunc)_add_quotes, NULL);
  g_list_free_full(raw_info, g_free);
  gchar *info = g_str_join_list(info_with_quotes, ", ");
  g_list_free_full(info_with_quotes, g_free);

  g_ptr_array_add(nodes, g_strdup_printf("{\"node\" : \"%p\", \"info\" : [%s]}", self, info));
  g_free(info);
};

static gchar *
nodes_as_json(GHashTable *nodes)
{
  GPtrArray *nodes_list = g_ptr_array_new_with_free_func(g_free);
  g_hash_table_foreach(nodes, (GHFunc)_append_node, nodes_list);
  g_ptr_array_add(nodes_list, NULL);

  gchar *nodes_joined = g_strjoinv(", ", (gchar **)nodes_list->pdata);
  g_ptr_array_free(nodes_list, TRUE);

  gchar *json = g_strdup_printf("[%s]", nodes_joined);
  g_free(nodes_joined);
  return json;
}

static GString *
generate_json(GHashTable *nodes, GHashTable *arcs)
{
  gchar *nodes_part = nodes_as_json(nodes);
  gchar *arcs_part = arcs_as_json(arcs);
  gchar *json = g_strdup_printf("{\"nodes\" : %s, \"arcs\" : %s}", nodes_part, arcs_part);
  GString *result = g_string_new(json);
  g_free(json);
  g_free(nodes_part);
  g_free(arcs_part);

  return result;
}

static gboolean
_add_arc_and_walk(LogPipe *from, LogPathConnectionType type, LogPipe *to, gpointer user_data)
{
  GHashTable *arcs = ((gpointer *) user_data)[1];

  Arc *arc = arc_new(from, to, type);
  g_hash_table_insert(arcs, arc, NULL);
  return TRUE;
}

static void
_walk_from_start_pipe(LogPipe *pipe, gpointer user_data)
{
  GHashTable *nodes = ((gpointer *) user_data)[0];

  if (g_hash_table_contains(nodes, pipe))
    return;

  g_hash_table_insert(nodes, pipe, NULL);

  log_pipe_walk(pipe, _add_arc_and_walk, user_data);
}

void
cfg_walker_get_graph(GPtrArray *start_nodes, GHashTable **nodes, GHashTable **arcs)
{
  *nodes = g_hash_table_new(g_direct_hash, g_direct_equal);
  *arcs = g_hash_table_new_full((GHashFunc)arc_hash, (GEqualFunc)arc_equal, (GDestroyNotify)arc_free, NULL);

  g_ptr_array_foreach(start_nodes, (GFunc) _walk_from_start_pipe, (gpointer [])
  {
    *nodes, *arcs
  });
}

GString *
cfg_walker_generate_graph(GlobalConfig *cfg)
{
  GHashTable *nodes;
  GHashTable *arcs;

  cfg_walker_get_graph(cfg->tree.initialized_pipes, &nodes, &arcs);
  GString *result = generate_json(nodes, arcs);
  g_hash_table_destroy(nodes);
  g_hash_table_destroy(arcs);
  return result;
}
