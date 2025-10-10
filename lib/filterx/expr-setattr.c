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
#include "filterx/expr-setattr.h"
#include "filterx/object-primitive.h"
#include "filterx/object-string.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-null.h"
#include "filterx/object-message-value.h"
#include "filterx/object-extractor.h"
#include "scratch-buffers.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"

typedef struct _FilterXSetAttr
{
  FilterXExpr super;
  FilterXExpr *object;
  FilterXObject *attr;
  FilterXExpr *new_value;
} FilterXSetAttr;

static inline FilterXObject *
_setattr(FilterXSetAttr *self, FilterXObject *object, FilterXObject *new_value)
{
  if (object->readonly)
    {
      filterx_eval_push_error_static_info("Failed to set-attribute to object", &self->super, "Object is readonly");
      return NULL;
    }

  FilterXObject *cloned = filterx_object_cow_fork2(filterx_object_ref(new_value), NULL);
  if (!filterx_object_setattr(object, self->attr, &cloned))
    {
      filterx_eval_push_error_static_info("Failed to set-attribute to object", &self->super, "setattr() method failed");
      filterx_object_unref(cloned);
      return NULL;
    }

  return cloned;
}

static inline FilterXObject *
_suppress_error(void)
{
  filterx_eval_dump_errors("FilterX: null coalesce assignment suppressing error");

  return filterx_null_new();
}

static FilterXObject *
_nullv_setattr_eval(FilterXExpr *s)
{
  FilterXSetAttr *self = (FilterXSetAttr *) s;
  FilterXObject *result = NULL;

  FilterXObject *new_value = filterx_expr_eval(self->new_value);
  if (!new_value || filterx_object_extract_null(new_value))
    {
      if (!new_value)
        return _suppress_error();

      return new_value;
    }

  FilterXObject *object = filterx_expr_eval_typed(self->object);
  if (!object)
    {
      filterx_eval_push_error_static_info("Failed to set-attribute to object", &self->super,
                                          "Failed to evaluate expression");
      goto exit;
    }

  result = _setattr(self, object, new_value);

exit:
  filterx_object_unref(new_value);
  filterx_object_unref(object);
  return result;
}

static FilterXObject *
_setattr_eval(FilterXExpr *s)
{
  FilterXSetAttr *self = (FilterXSetAttr *) s;
  FilterXObject *result = NULL;

  FilterXObject *new_value = filterx_expr_eval(self->new_value);
  if (!new_value)
    {
      filterx_eval_push_error_static_info("Failed to set-attribute to object", &self->super,
                                          "Failed to evaluate right hand side");
      return NULL;
    }

  FilterXObject *object = filterx_expr_eval_typed(self->object);
  if (!object)
    {
      filterx_eval_push_error_static_info("Failed to set-attribute to object", &self->super,
                                          "Failed to evaluate expression");
      goto exit;
    }

  result = _setattr(self, object, new_value);

exit:
  filterx_object_unref(new_value);
  filterx_object_unref(object);
  return result;
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXSetAttr *self = (FilterXSetAttr *) s;

  self->object = filterx_expr_optimize(self->object);
  self->new_value = filterx_expr_optimize(self->new_value);
  return NULL;
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXSetAttr *self = (FilterXSetAttr *) s;

  if (!filterx_expr_init(self->object, cfg))
    return FALSE;

  if (!filterx_expr_init(self->new_value, cfg))
    {
      filterx_expr_deinit(self->object, cfg);
      return FALSE;
    }

  return filterx_expr_init_method(s, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXSetAttr *self = (FilterXSetAttr *) s;

  filterx_expr_deinit(self->object, cfg);
  filterx_expr_deinit(self->new_value, cfg);
  filterx_expr_deinit_method(s, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXSetAttr *self = (FilterXSetAttr *) s;

  filterx_object_unref(self->attr);
  filterx_expr_unref(self->object);
  filterx_expr_unref(self->new_value);
  filterx_expr_free_method(s);
}

/* Takes reference of object and new_value */
FilterXExpr *
filterx_setattr_new(FilterXExpr *object, FilterXObject *attr_name, FilterXExpr *new_value)
{
  FilterXSetAttr *self = g_new0(FilterXSetAttr, 1);

  filterx_expr_init_instance(&self->super, "setattr", TRUE);
  self->super.eval = _setattr_eval;
  self->super.optimize = _optimize;
  self->super.init = _init;
  self->super.deinit = _deinit;
  self->super.free_fn = _free;
  self->object = object;

  g_assert(filterx_object_is_type(attr_name, &FILTERX_TYPE_NAME(string)));
  self->attr = attr_name;

  self->new_value = new_value;
  self->super.ignore_falsy_result = TRUE;

  /* NOTE: name borrows the string value from the string object */
  self->super.name = filterx_string_get_value_ref(self->attr, NULL);
  return &self->super;
}

FilterXExpr *
filterx_nullv_setattr_new(FilterXExpr *object, FilterXObject *attr_name, FilterXExpr *new_value)
{
  FilterXExpr *self = filterx_setattr_new(object, attr_name, new_value);
  self->type = "nullv_setattr";
  self->eval = _nullv_setattr_eval;
  return self;
}
