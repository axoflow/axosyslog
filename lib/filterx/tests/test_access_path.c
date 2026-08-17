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

/* The path ordering and the type env are both independent of the JIT, so unlike
 * test_type_inference.c this suite runs in the --disable-jit configuration too. */

#include <criterion/criterion.h>

#include "filterx/filterx-expr.h"
#include "filterx/filterx-type-inference.h"
#include "filterx/filterx-type-inference-private.h"
#include "filterx/expr-variable.h"
#include "filterx/expr-getattr.h"
#include "filterx/expr-get-subscript.h"
#include "filterx/expr-setattr.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-function.h"
#include "filterx/func-unset-empties.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"

#include "apphook.h"
#include "scratch-buffers.h"
#include "libtest/filterx-lib.h"

static FilterXAccessPath
_path_of(FilterXVariableHandle root, const gchar *first, ...)
{
  FilterXAccessPath path;
  va_list ap;

  memset(&path, 0, sizeof(path));
  path.root = root;

  va_start(ap, first);
  for (const gchar *key = first; key != NULL; key = va_arg(ap, const gchar *))
    filterx_access_path_append_step(&path, filterx_access_path_intern_key(key));
  va_end(ap);

  return path;
}

#define PATH(root, ...) _path_of(root, __VA_ARGS__, NULL)
#define ROOT(root)      _path_of(root, NULL)

static gint
_compare_sign(const FilterXAccessPath *a, const FilterXAccessPath *b)
{
  gint c = filterx_access_path_compare(a, b, NULL);
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

/* --- interning ------------------------------------------------------------------------------ */

Test(filterx_access_path, equal_keys_intern_to_one_pointer)
{
  gchar *a = g_strdup("hello");
  gchar *b = g_strdup("hello");

  cr_assert_eq(filterx_access_path_intern_key(a), filterx_access_path_intern_key(b));
  cr_assert_neq(filterx_access_path_intern_key("hello"), filterx_access_path_intern_key("world"));

  g_free(a);
  g_free(b);
}

Test(filterx_access_path, interning_a_null_key_yields_null)
{
  cr_assert_null(filterx_access_path_intern_key(NULL));
}

Test(filterx_access_path, an_unnameable_step_truncates_the_path)
{
  FilterXAccessPath path = ROOT(7);

  cr_assert_not(filterx_access_path_append_step(&path, NULL));
  cr_assert(path.truncated);
  cr_assert_eq(path.n_steps, 0);
}

Test(filterx_access_path, a_step_past_an_unnameable_one_does_not_reattach)
{
  /* d[$k].a must not read as d.a: the write went under some key of d, not under that one.  Once
   * a path has lost its address every further step keeps it lost. */
  FilterXAccessPath path = ROOT(7);

  cr_assert_not(filterx_access_path_append_step(&path, NULL));
  cr_assert_not(filterx_access_path_append_step(&path, filterx_access_path_intern_key("a")));
  cr_assert(path.truncated);
  cr_assert_eq(path.n_steps, 0);
}

Test(filterx_access_path, an_interned_key_outlives_the_string_it_was_made_from)
{
  gchar *transient = g_strdup("borrowed");
  const gchar *interned = filterx_access_path_intern_key(transient);

  memset(transient, 'x', strlen(transient));
  g_free(transient);

  cr_assert_str_eq(interned, "borrowed");
  cr_assert_eq(filterx_access_path_intern_key("borrowed"), interned);
}

Test(filterx_access_path, clearing_the_key_pool_keeps_interning_usable)
{
  filterx_access_path_intern_key("recycled");

  filterx_access_path_release_keys();

  const gchar *after = filterx_access_path_intern_key("recycled");
  cr_assert_str_eq(after, "recycled");
  cr_assert_eq(filterx_access_path_intern_key("recycled"), after);
}

/* --- ordering ------------------------------------------------------------------------------- */

Test(filterx_access_path, roots_order_numerically_and_never_interleave)
{
  FilterXAccessPath a = ROOT(7);
  FilterXAccessPath a_deep = PATH(7, "z");
  FilterXAccessPath b = ROOT(9);

  cr_assert_eq(_compare_sign(&a, &b), -1);
  cr_assert_eq(_compare_sign(&a_deep, &b), -1);
  cr_assert_eq(_compare_sign(&b, &a_deep), 1);
}

Test(filterx_access_path, a_path_sorts_before_its_own_descendants)
{
  FilterXAccessPath p = PATH(7, "cfg");
  FilterXAccessPath child = PATH(7, "cfg", "net");
  FilterXAccessPath grandchild = PATH(7, "cfg", "net", "port");

  cr_assert_eq(_compare_sign(&p, &child), -1);
  cr_assert_eq(_compare_sign(&child, &grandchild), -1);
}

Test(filterx_access_path, a_shared_character_prefix_is_not_a_shared_path_prefix)
{
  FilterXAccessPath sub = PATH(7, "sub");
  FilterXAccessPath subscription = PATH(7, "subscription");
  FilterXAccessPath sub_child = PATH(7, "sub", "x");

  cr_assert_not(filterx_access_path_is_prefix_of(&sub, &subscription));
  cr_assert(filterx_access_path_is_prefix_of(&sub, &sub_child));

  /* ... and the ordering agrees: sub's descendants all land before subscription. */
  cr_assert_eq(_compare_sign(&sub, &sub_child), -1);
  cr_assert_eq(_compare_sign(&sub_child, &subscription), -1);
}

Test(filterx_access_path, a_path_is_immediately_followed_by_exactly_its_descendants)
{
  FilterXAccessPath paths[] =
  {
    PATH(9, "a"),
    PATH(7, "sub", "x"),
    ROOT(7),
    PATH(7, "cfg", "net", "port"),
    PATH(7, "subscription"),
    PATH(7, "cfg"),
    ROOT(9),
    PATH(7, "sub"),
    PATH(7, "cfg", "net"),
  };
  guint n = G_N_ELEMENTS(paths);

  qsort(paths, n, sizeof(paths[0]), (int (*)(const void *, const void *)) filterx_access_path_compare);

  for (guint i = 0; i < n; i++)
    {
      gboolean left_the_subtree = FALSE;
      for (guint j = i + 1; j < n; j++)
        {
          if (!filterx_access_path_is_prefix_of(&paths[i], &paths[j]))
            left_the_subtree = TRUE;
          else
            cr_assert_not(left_the_subtree,
                          "entry %u is a descendant of %u but a non-descendant sorted between them", j, i);
        }
    }

  /* And spot-check the order the invariant produces. */
  cr_assert_eq(_compare_sign(&paths[0], &paths[1]), -1);
  cr_assert_eq(paths[0].root, 7);
  cr_assert_eq(paths[0].n_steps, 0);
  cr_assert_eq(paths[1].n_steps, 1);
  cr_assert_eq(paths[1].steps[0], filterx_access_path_intern_key("cfg"));
}

/* --- the env -------------------------------------------------------------------------------- */

static FilterXStaticType
_static_type_at(FilterXTypeEnv *env, FilterXAccessPath path)
{
  return filterx_type_env_get_static_type_at_path(env, &path);
}

static void
_set(FilterXTypeEnv *env, FilterXAccessPath path, FilterXStaticType static_type, gboolean closed)
{
  filterx_type_env_set_at_path(env, &path, static_type, closed);
}

Test(filterx_type_env, an_entry_of_an_unknown_static_type_is_not_the_same_as_no_entry)
{
  /* A closed container holding one key whose type nobody knows must not read as empty. */
  FilterXTypeEnv *env = filterx_type_env_new();

  _set(env, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(env, PATH(7, "a"), FILTERX_STATIC_TYPE_UNKNOWN, FALSE);

  FilterXAccessPath present = PATH(7, "a");
  FilterXAccessPath absent = PATH(7, "b");

  cr_assert(filterx_type_env_get_fact_at_path(env, &present, NULL, NULL));
  cr_assert_not(filterx_type_env_get_fact_at_path(env, &absent, NULL, NULL));

  filterx_type_env_free(env);
}

Test(filterx_type_env, a_write_replaces_one_location_and_leaves_its_siblings_alone)
{
  FilterXTypeEnv *env = filterx_type_env_new();

  _set(env, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(env, PATH(7, "a"), FILTERX_STATIC_TYPE_STRING, FALSE);
  _set(env, PATH(7, "b"), FILTERX_STATIC_TYPE_INTEGER, FALSE);

  /* d.a = "s"; d.a = 1;  -- an overwrite, not a meet. */
  _set(env, PATH(7, "a"), FILTERX_STATIC_TYPE_INTEGER, FALSE);

  cr_assert_eq(_static_type_at(env, PATH(7, "a")), FILTERX_STATIC_TYPE_INTEGER);
  cr_assert_eq(_static_type_at(env, PATH(7, "b")), FILTERX_STATIC_TYPE_INTEGER);

  filterx_type_env_free(env);
}

Test(filterx_type_env, writing_a_new_key_keeps_the_parent_closed)
{
  FilterXTypeEnv *env = filterx_type_env_new();

  _set(env, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(env, PATH(7, "a"), FILTERX_STATIC_TYPE_STRING, FALSE);

  FilterXAccessPath root = ROOT(7);
  gboolean closed = FALSE;
  cr_assert(filterx_type_env_get_fact_at_path(env, &root, NULL, &closed));
  cr_assert(closed);

  filterx_type_env_free(env);
}

Test(filterx_type_env, an_unrecorded_key_reads_as_unknown_whether_its_container_is_open_or_closed)
{
  FilterXTypeEnv *env = filterx_type_env_new();

  _set(env, ROOT(7), FILTERX_STATIC_TYPE_DICT, FALSE);
  _set(env, PATH(7, "x"), FILTERX_STATIC_TYPE_STRING, FALSE);

  cr_assert_eq(_static_type_at(env, PATH(7, "x")), FILTERX_STATIC_TYPE_STRING);
  cr_assert_eq(_static_type_at(env, PATH(7, "whatever")), FILTERX_STATIC_TYPE_UNKNOWN);

  _set(env, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(env, PATH(7, "x"), FILTERX_STATIC_TYPE_STRING, FALSE);
  cr_assert_eq(_static_type_at(env, PATH(7, "whatever")), FILTERX_STATIC_TYPE_UNKNOWN);

  filterx_type_env_free(env);
}

Test(filterx_type_env, a_truncated_path_reads_as_unknown)
{
  /* d[$k] addresses no location, so nothing recorded under d answers for it. */
  FilterXTypeEnv *env = filterx_type_env_new();

  _set(env, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(env, PATH(7, "x"), FILTERX_STATIC_TYPE_INTEGER, FALSE);

  FilterXAccessPath dynamic = ROOT(7);
  filterx_access_path_append_step(&dynamic, NULL);

  cr_assert(dynamic.truncated);
  cr_assert_eq(_static_type_at(env, dynamic), FILTERX_STATIC_TYPE_UNKNOWN);

  filterx_type_env_free(env);
}

Test(filterx_type_env, unset_of_a_named_key_keeps_the_parent_closed)
{
  FilterXTypeEnv *env = filterx_type_env_new();

  _set(env, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(env, PATH(7, "port"), FILTERX_STATIC_TYPE_INTEGER, FALSE);
  _set(env, PATH(7, "host"), FILTERX_STATIC_TYPE_STRING, FALSE);

  FilterXAccessPath port = PATH(7, "port");
  filterx_type_env_clear_at_path(env, &port);

  FilterXAccessPath root = ROOT(7);
  gboolean closed = FALSE;
  cr_assert(filterx_type_env_get_fact_at_path(env, &root, NULL, &closed));
  cr_assert(closed, "removing a named key shrinks the key set, it does not make it unknown");
  cr_assert_eq(_static_type_at(env, PATH(7, "port")), FILTERX_STATIC_TYPE_UNKNOWN);
  cr_assert_eq(_static_type_at(env, PATH(7, "host")), FILTERX_STATIC_TYPE_STRING);

  filterx_type_env_free(env);
}

Test(filterx_type_env, a_write_through_an_unnameable_key_empties_the_container_it_hit)
{
  /* d[$k] = 42 may have landed on any key of d, so d keeps its static type and loses its whole interior. */
  FilterXTypeEnv *env = filterx_type_env_new();

  _set(env, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(env, PATH(7, "net"), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(env, PATH(7, "net", "host"), FILTERX_STATIC_TYPE_STRING, FALSE);
  _set(env, PATH(7, "tls"), FILTERX_STATIC_TYPE_DICT, TRUE);

  FilterXAccessPath dynamic = ROOT(7);
  filterx_access_path_append_step(&dynamic, NULL);
  filterx_type_env_set_shape_at_path(env, &dynamic, NULL);

  cr_assert_eq(_static_type_at(env, ROOT(7)), FILTERX_STATIC_TYPE_DICT,
               "the write reached into d, it did not replace it");
  cr_assert_eq(_static_type_at(env, PATH(7, "net")), FILTERX_STATIC_TYPE_UNKNOWN);
  cr_assert_eq(_static_type_at(env, PATH(7, "net", "host")), FILTERX_STATIC_TYPE_UNKNOWN);
  cr_assert_eq(_static_type_at(env, PATH(7, "tls")), FILTERX_STATIC_TYPE_UNKNOWN);

  FilterXAccessPath root = ROOT(7);
  gboolean closed = TRUE;
  cr_assert(filterx_type_env_get_fact_at_path(env, &root, NULL, &closed));
  cr_assert_not(closed, "the key the write landed on is not among the recorded ones");

  /* ... and a key written afterwards is then simply what that key is. */
  _set(env, PATH(7, "a"), FILTERX_STATIC_TYPE_INTEGER, FALSE);
  cr_assert_eq(_static_type_at(env, PATH(7, "a")), FILTERX_STATIC_TYPE_INTEGER);

  filterx_type_env_free(env);
}

/* --- the join ------------------------------------------------------------------------------- */

Test(filterx_type_env, meet_keeps_a_key_the_other_side_proves_absent)
{
  /* d = {}; if (c) { d.a = 1; }  -- the else side still has d closed, which proves "a" absent
   * there, so it constrains nothing and the written branch's type survives the join. */
  FilterXTypeEnv *dst = filterx_type_env_new();
  FilterXTypeEnv *src = filterx_type_env_new();

  _set(dst, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(dst, PATH(7, "a"), FILTERX_STATIC_TYPE_INTEGER, FALSE);
  _set(src, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);

  filterx_type_env_meet_into(dst, src);

  cr_assert_eq(_static_type_at(dst, PATH(7, "a")), FILTERX_STATIC_TYPE_INTEGER);
  cr_assert_eq(_static_type_at(dst, ROOT(7)), FILTERX_STATIC_TYPE_DICT);

  filterx_type_env_free(dst);
  filterx_type_env_free(src);
}

Test(filterx_type_env, meet_drops_a_key_the_other_side_cannot_vouch_for)
{
  /* d = {}; d[$k] = "s"; if (c) { d.a = 1; }  -- the other side left d open, so it may hold "a" at
   * a type nobody here has seen, and d.a must not answer INTEGER. */
  FilterXTypeEnv *dst = filterx_type_env_new();
  FilterXTypeEnv *src = filterx_type_env_new();

  _set(dst, ROOT(7), FILTERX_STATIC_TYPE_DICT, FALSE);
  _set(dst, PATH(7, "a"), FILTERX_STATIC_TYPE_INTEGER, FALSE);
  _set(src, ROOT(7), FILTERX_STATIC_TYPE_DICT, FALSE);

  filterx_type_env_meet_into(dst, src);

  FilterXAccessPath a = PATH(7, "a");
  cr_assert_not(filterx_type_env_get_fact_at_path(dst, &a, NULL, NULL));
  cr_assert_eq(_static_type_at(dst, PATH(7, "a")), FILTERX_STATIC_TYPE_UNKNOWN);
  cr_assert_eq(_static_type_at(dst, PATH(7, "an_unrelated_key")), FILTERX_STATIC_TYPE_UNKNOWN,
               "d.a's type must not have been published over any other key either");

  filterx_type_env_free(dst);
  filterx_type_env_free(src);
}

Test(filterx_type_env, meet_gives_up_closed_on_the_parent_of_a_key_it_drops)
{
  /* `closed` bounds the key set from above by the recorded children.  Dropping one of those
   * children without proving it gone tightens that bound onto a key set that did not shrink, so
   * the claim has to go with it. */
  FilterXTypeEnv *dst = filterx_type_env_new();
  FilterXTypeEnv *src = filterx_type_env_new();

  _set(dst, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(dst, PATH(7, "a"), FILTERX_STATIC_TYPE_INTEGER, FALSE);

  /* src is open, so it cannot prove "a" absent -- and its own key set is not bounded either. */
  _set(src, ROOT(7), FILTERX_STATIC_TYPE_DICT, FALSE);

  filterx_type_env_meet_into(dst, src);

  FilterXAccessPath root = ROOT(7);
  gboolean closed = TRUE;
  cr_assert(filterx_type_env_get_fact_at_path(dst, &root, NULL, &closed));
  cr_assert_not(closed, "d would otherwise claim to have no keys at all");

  filterx_type_env_free(dst);
  filterx_type_env_free(src);
}

Test(filterx_type_env, meet_keeps_a_disagreeing_key_present_at_an_unknown_static_type)
{
  FilterXTypeEnv *dst = filterx_type_env_new();
  FilterXTypeEnv *src = filterx_type_env_new();

  _set(dst, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(dst, PATH(7, "a"), FILTERX_STATIC_TYPE_INTEGER, FALSE);
  _set(src, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(src, PATH(7, "a"), FILTERX_STATIC_TYPE_STRING, FALSE);

  filterx_type_env_meet_into(dst, src);

  FilterXAccessPath a = PATH(7, "a");
  cr_assert(filterx_type_env_get_fact_at_path(dst, &a, NULL, NULL),
            "both sides claim the key exists, so it stays recorded even at an unknown static type");
  cr_assert_eq(_static_type_at(dst, PATH(7, "a")), FILTERX_STATIC_TYPE_UNKNOWN);

  filterx_type_env_free(dst);
  filterx_type_env_free(src);
}

Test(filterx_type_env, meet_gives_up_closed_when_the_other_side_knows_of_an_extra_key)
{
  FilterXTypeEnv *dst = filterx_type_env_new();
  FilterXTypeEnv *src = filterx_type_env_new();

  _set(dst, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(src, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(src, PATH(7, "extra"), FILTERX_STATIC_TYPE_INTEGER, FALSE);

  filterx_type_env_meet_into(dst, src);

  FilterXAccessPath root = ROOT(7);
  gboolean closed = TRUE;
  cr_assert(filterx_type_env_get_fact_at_path(dst, &root, NULL, &closed));
  cr_assert_not(closed, "dst's key set was only complete on dst's own path");

  filterx_type_env_free(dst);
  filterx_type_env_free(src);
}

Test(filterx_type_env, meet_drops_a_variable_the_other_side_says_nothing_about)
{
  FilterXTypeEnv *dst = filterx_type_env_new();
  FilterXTypeEnv *src = filterx_type_env_new();

  _set(dst, ROOT(7), FILTERX_STATIC_TYPE_STRING, FALSE);

  filterx_type_env_meet_into(dst, src);

  cr_assert_eq(_static_type_at(dst, ROOT(7)), FILTERX_STATIC_TYPE_UNKNOWN);

  filterx_type_env_free(dst);
  filterx_type_env_free(src);
}

/* --- depth ---------------------------------------------------------------------------------- */

Test(filterx_access_path, a_path_past_the_depth_cap_is_truncated_not_rejected)
{
  FilterXAccessPath path;

  memset(&path, 0, sizeof(path));
  path.root = 7;
  for (guint i = 0; i < FILTERX_ACCESS_PATH_MAX_DEPTH; i++)
    cr_assert(filterx_access_path_append_step(&path, filterx_access_path_intern_key("k")));

  cr_assert_not(filterx_access_path_append_step(&path, filterx_access_path_intern_key("one_too_many")));
  cr_assert(path.truncated);
  cr_assert_eq(path.n_steps, FILTERX_ACCESS_PATH_MAX_DEPTH);
}

Test(filterx_type_env, a_truncated_write_opens_the_deepest_addressable_ancestor)
{
  FilterXTypeEnv *env = filterx_type_env_new();
  FilterXAccessPath deep;

  memset(&deep, 0, sizeof(deep));
  deep.root = 7;
  for (guint i = 0; i < FILTERX_ACCESS_PATH_MAX_DEPTH; i++)
    filterx_access_path_append_step(&deep, filterx_access_path_intern_key("k"));

  _set(env, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  filterx_type_env_set_at_path(env, &deep, FILTERX_STATIC_TYPE_DICT, TRUE);

  FilterXAccessPath over = deep;
  over.truncated = TRUE;
  filterx_type_env_set_shape_at_path(env, &over, NULL);

  /* The root is untouched: only the ancestor that could not address the write gives up its
   * complete key set. */
  cr_assert_eq(_static_type_at(env, ROOT(7)), FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(_static_type_at(env, deep), FILTERX_STATIC_TYPE_DICT);

  gboolean closed = TRUE;
  cr_assert(filterx_type_env_get_fact_at_path(env, &deep, NULL, &closed));
  cr_assert_not(closed);

  filterx_type_env_free(env);
}

/* --- cloning -------------------------------------------------------------------------------- */

static gboolean
_is_root_seven(FilterXVariableHandle handle, gpointer user_data)
{
  return handle == 7;
}

Test(filterx_type_env, a_filtered_clone_selects_on_the_root_of_the_path)
{
  FilterXTypeEnv *env = filterx_type_env_new();

  _set(env, ROOT(7), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(env, PATH(7, "a"), FILTERX_STATIC_TYPE_STRING, FALSE);
  _set(env, ROOT(9), FILTERX_STATIC_TYPE_DICT, TRUE);
  _set(env, PATH(9, "a"), FILTERX_STATIC_TYPE_STRING, FALSE);

  FilterXTypeEnv *clone = filterx_type_env_clone_filtered(env, _is_root_seven, NULL);

  cr_assert_eq(_static_type_at(clone, PATH(7, "a")), FILTERX_STATIC_TYPE_STRING);
  cr_assert_eq(_static_type_at(clone, ROOT(9)), FILTERX_STATIC_TYPE_UNKNOWN);

  FilterXAccessPath nine_a = PATH(9, "a");
  cr_assert_not(filterx_type_env_get_fact_at_path(clone, &nine_a, NULL, NULL));

  filterx_type_env_free(clone);
  filterx_type_env_free(env);
}

/* --- peeling an expression ------------------------------------------------------------------ */

/* The steps are interned, so the path outlives the expression. */
static gboolean
_peel_path_and_free(FilterXExpr *expr, FilterXAccessPath *path_out)
{
  /* construct, optimize, then use -- this suite deliberately skips filterx_expr_init() */
  expr = filterx_expr_optimize(expr);

  gboolean addressable = filterx_expr_get_path(expr, path_out);

  filterx_expr_unref(expr);
  return addressable;
}

#define MSG_ROOT(name) filterx_map_varname_to_handle(name, FX_VAR_MESSAGE_TIED)

Test(filterx_access_path, a_getattr_chain_peels_to_its_root_variable)
{
  FilterXAccessPath path;
  FilterXAccessPath expected = PATH(MSG_ROOT("a"), "b", "c");

  cr_assert(_peel_path_and_free(filterx_getattr_new(
                                  filterx_getattr_new(filterx_msg_variable_expr_new("a"),
                                                      filterx_string_new("b", -1)),
                                  filterx_string_new("c", -1)),
                                &path));

  cr_assert_eq(_compare_sign(&path, &expected), 0);
  cr_assert_not(path.truncated);
}

Test(filterx_access_path, a_getattr_over_a_macro_variable_is_not_addressable)
{
  /* A macro is recomputed from the message on every read, so there is nothing to record about it
   * and -- since every assignment forks its RHS -- nothing that can alias it either. */
  FilterXAccessPath path;

  cr_assert_not(_peel_path_and_free(filterx_getattr_new(filterx_msg_variable_expr_new("FACILITY"),
                                                        filterx_string_new("b", -1)),
                                    &path));
}

Test(filterx_access_path, a_getattr_over_a_function_call_is_not_addressable)
{
  /* filterx_type_env_open_arguments() walks arbitrary argument expressions and depends on this to
   * not open a container it cannot name. */
  GError *error = NULL;
  GList *args = g_list_append(NULL, filterx_function_arg_new(NULL, filterx_floating_variable_expr_new("d")));
  FilterXExpr *call = filterx_function_unset_empties_new(filterx_function_args_new(args, &error), &error);
  FilterXAccessPath path;

  cr_assert_null(error);
  cr_assert_not(_peel_path_and_free(filterx_getattr_new(call, filterx_string_new("b", -1)), &path));
}

Test(filterx_access_path, a_literal_string_subscript_names_what_a_getattr_names)
{
  FilterXAccessPath by_attr, by_subscript;

  cr_assert(_peel_path_and_free(filterx_getattr_new(filterx_msg_variable_expr_new("a"),
                                                    filterx_string_new("b", -1)),
                                &by_attr));
  cr_assert(_peel_path_and_free(filterx_get_subscript_new(filterx_msg_variable_expr_new("a"),
                                                          filterx_literal_new(filterx_string_new("b", -1))),
                                &by_subscript));

  cr_assert_eq(_compare_sign(&by_attr, &by_subscript), 0);
  cr_assert_eq(by_attr.steps[0], by_subscript.steps[0]);   /* interned, so one pointer */
}

Test(filterx_access_path, a_list_index_and_a_computed_key_both_truncate_the_path)
{
  /* filterx_sequence_normalize_index() resolves an index against the runtime length, so l[-1] and
   * l[0] can name the same slot and no literal integer earns a step of its own. */
  FilterXAccessPath by_index, by_computed;

  cr_assert(_peel_path_and_free(filterx_get_subscript_new(filterx_msg_variable_expr_new("l"),
                                                          filterx_literal_new(filterx_integer_new(0))),
                                &by_index));
  cr_assert(_peel_path_and_free(filterx_get_subscript_new(filterx_msg_variable_expr_new("l"),
                                                          filterx_floating_variable_expr_new("k")),
                                &by_computed));

  cr_assert(by_index.truncated);
  cr_assert_eq(by_index.n_steps, 0);
  cr_assert_eq(by_index.root, MSG_ROOT("l"));
  cr_assert(by_computed.truncated);
  cr_assert_eq(by_computed.n_steps, 0);
  cr_assert_eq(by_computed.root, MSG_ROOT("l"));
}

Test(filterx_access_path, a_getattr_over_a_dynamic_subscript_stays_truncated_at_the_container)
{
  /* d[$k].a names a location under some key of d, and which one is not knowable here. */
  FilterXAccessPath path;

  cr_assert(_peel_path_and_free(filterx_getattr_new(
                                  filterx_get_subscript_new(filterx_msg_variable_expr_new("d"),
                                                            filterx_floating_variable_expr_new("k")),
                                  filterx_string_new("a", -1)),
                                &path));

  cr_assert(path.truncated);
  cr_assert_eq(path.n_steps, 0);
  cr_assert_eq(path.root, MSG_ROOT("d"));
}

Test(filterx_access_path, a_chain_past_the_depth_cap_keeps_its_root_nearest_steps)
{
  FilterXExpr *expr = filterx_msg_variable_expr_new("a");
  gchar key[2] = { 0, 0 };
  FilterXAccessPath path;

  for (guint i = 0; i < FILTERX_ACCESS_PATH_MAX_DEPTH + 1; i++)
    {
      key[0] = 'a' + i;
      expr = filterx_getattr_new(expr, filterx_string_new(key, -1));
    }

  cr_assert(_peel_path_and_free(expr, &path));

  /* Assembled root-first, so it is the deep end that falls off. */
  cr_assert(path.truncated);
  cr_assert_eq(path.n_steps, FILTERX_ACCESS_PATH_MAX_DEPTH);
  cr_assert_eq(path.steps[0], filterx_access_path_intern_key("a"));
  cr_assert_eq(path.steps[FILTERX_ACCESS_PATH_MAX_DEPTH - 1], filterx_access_path_intern_key("h"));
}

Test(filterx_access_path, a_setattr_names_the_location_it_writes)
{
  /* Safe because the grammar reaches an assignment only from stmt_expr, so it is never read from. */
  FilterXAccessPath path;
  FilterXAccessPath expected = PATH(MSG_ROOT("a"), "b", "c");

  cr_assert(_peel_path_and_free(filterx_setattr_new(
                                  filterx_getattr_new(filterx_msg_variable_expr_new("a"),
                                                      filterx_string_new("b", -1)),
                                  filterx_string_new("c", -1),
                                  filterx_literal_new(filterx_integer_new(1))),
                                &path));

  cr_assert_eq(_compare_sign(&path, &expected), 0);
}

static void
setup(void)
{
  app_startup();
  init_libtest_filterx();
}

static void
teardown(void)
{
  deinit_libtest_filterx();
  scratch_buffers_explicit_gc();
  app_shutdown();
}

TestSuite(filterx_access_path, .init = setup, .fini = teardown);
TestSuite(filterx_type_env, .init = setup, .fini = teardown);
