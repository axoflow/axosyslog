/*
 * Copyright (c) 2002-2018 Balabit
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

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include "libtest/parameterized.h"

#include "logmsg/type-hinting.h"
#include "apphook.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef struct _StringHintPair
{
  gchar *string;
  LogMessageValueType value;
} StringHintPair;

typedef struct _StringBoolPair
{
  gchar *string;
  gboolean value;
} StringBoolPair;

typedef struct _StringDoublePair
{
  gchar *string;
  gdouble value;
} StringDoublePair;

typedef struct _StringUInt64Pair
{
  gchar *string;
  guint64 value;
} StringUInt64Pair;


static StringHintPair test_type_hint_parse_params[] =
{
  {"string",    LM_VT_STRING},
  {"literal",   LM_VT_JSON},
  {"json",      LM_VT_JSON},
  {"boolean",   LM_VT_BOOLEAN},
  {"int",       LM_VT_INTEGER},
  {"int32",     LM_VT_INTEGER},
  {"int64",     LM_VT_INTEGER},
  {"float",     LM_VT_DOUBLE},
  {"double",    LM_VT_DOUBLE},
  {"datetime",  LM_VT_DATETIME},
  {"list",      LM_VT_LIST},
  {"null",      LM_VT_NULL},
  {"bytes",     LM_VT_BYTES},
  {"protobuf",  LM_VT_PROTOBUF},
};

StaticParameterizedTest(StringHintPair *string_value_pair, test_type_hint_parse_params, type_hints,
                        test_type_hint_parse)
{
  LogMessageValueType type_hint;
  GError *error = NULL;

  cr_assert(type_hint_parse(string_value_pair->string, &type_hint, &error),
            "Parsing \"%s\" as type hint failed", string_value_pair->string);
  cr_assert(eq(int, type_hint, string_value_pair->value));
  cr_assert(zero(ptr, error));
}

Test(type_hints, test_invalid_type_hint_parse)
{
  LogMessageValueType t;
  GError *error = NULL;
  cr_assert(not(type_hint_parse("invalid-hint", &t, &error)),
            "Parsing an invalid hint results in an error.");

  cr_assert(not(zero(ptr, error)));
  cr_assert(eq(u32, error->domain, TYPE_HINTING_ERROR));
  cr_assert(eq(int, error->code, TYPE_HINTING_INVALID_TYPE));
  g_clear_error(&error);
}

static StringBoolPair test_bool_cast_params[] =
{
  {"True",  TRUE},
  {"true",  TRUE},
  {"1",     TRUE},
  {"totaly true", TRUE},
  {"False", FALSE},
  {"false", FALSE},
  {"0",     FALSE},
  {"fatally false", FALSE}
};

StaticParameterizedTest(StringBoolPair *string_value_pair, test_bool_cast_params, type_hints, test_bool_cast)
{
  gboolean value;
  GError *error = NULL;

  cr_assert(type_cast_to_boolean(string_value_pair->string, -1, &value, &error),
            "Type cast of \"%s\" to gboolean failed", string_value_pair->string);
  cr_assert(eq(int, value, string_value_pair->value));
  cr_assert(zero(ptr, error));
}

Test(type_hints, test_invalid_bool_cast)
{
  GError *error = NULL;
  gboolean value;

  /* test invalid boolean value cast */
  cr_assert(not(type_cast_to_boolean("booyah", -1, &value, &error)),
            "Type cast \"booyah\" to gboolean should be failed");
  cr_assert(not(zero(ptr, error)));
  cr_assert(eq(u32, error->domain, TYPE_HINTING_ERROR));
  cr_assert(eq(int, error->code, TYPE_HINTING_INVALID_CAST));

  g_clear_error(&error);
}

Test(type_hints, test_int32_cast)
{
  GError *error = NULL;
  gint32 value;

  cr_assert(type_cast_to_int32("12345", -1, &value, &error), "Type cast of \"12345\" to gint32 failed");
  cr_assert(eq(i32, value, 12345));
  cr_assert(zero(ptr, error));

  cr_assert(type_cast_to_int32("0x1000", -1, &value, &error), "Type cast of \"0x1000\" to gint32 failed");
  cr_assert(eq(i32, value, 0x1000));
  cr_assert(zero(ptr, error));

  cr_assert(type_cast_to_int32("0111", -1, &value, &error), "Type cast of \"0111\" to gint32 failed");
  cr_assert(eq(i32, value, 111));
  cr_assert(zero(ptr, error));

  /* test for invalid string */
  cr_assert(not(type_cast_to_int32("12345a", -1, &value, &error)),
            "Type cast of invalid string to gint32 should be failed");
  cr_assert(not(zero(ptr, error)));
  cr_assert(eq(u32, error->domain, TYPE_HINTING_ERROR));
  cr_assert(eq(int, error->code, TYPE_HINTING_INVALID_CAST));
  g_clear_error(&error);

  /* empty string */
  cr_assert(not(type_cast_to_int32("", -1, &value, &error)),
            "Type cast of empty string to gint32 should be failed");
  cr_assert(not(zero(ptr, error)));
  cr_assert(eq(u32, error->domain, TYPE_HINTING_ERROR));
  cr_assert(eq(int, error->code, TYPE_HINTING_INVALID_CAST));

  g_clear_error(&error);
}

Test(type_hints, test_int32_nonzero_terminated)
{
  GError *error = NULL;
  gint32 int32_value;
  gint64 int64_value;
  gboolean bool_value;
  gdouble dbl_value;
  UnixTime ut_value;

  cr_assert(type_cast_to_int32("12345", 3, &int32_value, &error),
            "Type cast of non-zero terminated \"123\" to gint32 failed");
  cr_assert(eq(i32, int32_value, 123));
  cr_assert(zero(ptr, error));

  cr_assert(type_cast_to_int64("12345", 3, &int64_value, &error),
            "Type cast of non-zero terminated \"123\" to gint64 failed");
  cr_assert(eq(i64, int64_value, 123));
  cr_assert(zero(ptr, error));

  cr_assert(type_cast_to_boolean("12", 1, &bool_value, &error),
            "Type cast of non-zero terminated \"1\" to gboolean failed");
  cr_assert(eq(int, bool_value, 1));
  cr_assert(zero(ptr, error));

  cr_assert(type_cast_to_double("123456", 3, &dbl_value, &error),
            "Type cast of non-zero terminated \"123\" to gdouble failed");
  cr_assert(lt(dbl, dbl_value - 123, 0.1));
  cr_assert(zero(ptr, error));

  cr_assert(type_cast_to_datetime_unixtime("1699134067.123", 12, &ut_value, &error),
            "Type cast of non-zero terminated \"1699134067.1\" to unixtime failed");
  cr_assert(eq(i64, ut_value.ut_sec, 1699134067));
  cr_assert(eq(u32, ut_value.ut_usec, 100000));
  cr_assert(zero(ptr, error));
}

Test(type_hints, test_int64_cast)
{
  GError *error = NULL;
  gint64 value;

  cr_assert(type_cast_to_int64("12345", -1, &value, &error), "Type cast of \"12345\" to gint64 failed");
  cr_assert(eq(i64, value, 12345));
  cr_assert(zero(ptr, error));

  cr_assert(type_cast_to_int64("0x1000", -1, &value, &error), "Type cast of \"0x1000\" to gint64 failed");
  cr_assert(eq(i64, value, 0x1000));
  cr_assert(zero(ptr, error));

  cr_assert(type_cast_to_int64("0111", -1, &value, &error), "Type cast of \"0111\" to gint64 failed");
  cr_assert(eq(i64, value, 111));
  cr_assert(zero(ptr, error));


  /* test for invalid string */
  cr_assert(not(type_cast_to_int64("12345a", -1, &value, &error)),
            "Type cast of invalid string to gint64 should be failed");
  cr_assert(not(zero(ptr, error)));
  cr_assert(eq(u32, error->domain, TYPE_HINTING_ERROR));
  cr_assert(eq(int, error->code, TYPE_HINTING_INVALID_CAST));
  g_clear_error(&error);

  /* empty string */
  cr_assert(not(type_cast_to_int64("", -1, &value, &error)),
            "Type cast of empty string to gint64 should be failed");
  cr_assert(not(zero(ptr, error)));
  cr_assert(eq(u32, error->domain, TYPE_HINTING_ERROR));
  cr_assert(eq(int, error->code, TYPE_HINTING_INVALID_CAST));
  g_clear_error(&error);
}

static StringDoublePair test_double_cast_params[] =
{
#ifdef INFINITY
  {"INF", (gdouble)INFINITY},
#endif
  {"1.0", 1.0},
  {"1e-100000000", 0.0}
};

static void
cr_assert_gdouble_eq(gdouble a, gdouble b)
{
  const gint is_a_inf = isinf(a);
  const gint is_b_inf = isinf(b);

  cr_assert(eq(int, is_a_inf, is_b_inf));
  if (is_a_inf && is_b_inf)
    return;

  cr_assert(epsilon_eq(dbl, a, b, G_MINDOUBLE));
}

StaticParameterizedTest(StringDoublePair *string_value_pair, test_double_cast_params, type_hints, test_double_cast)
{
  gdouble value;
  GError *error = NULL;

  cr_assert(type_cast_to_double(string_value_pair->string, -1, &value, &error),
            "Type cast of \"%s\" to double failed", string_value_pair->string);

  cr_assert_gdouble_eq(value, string_value_pair->value);
  cr_assert(zero(ptr, error));
}

static StringDoublePair test_invalid_double_cast_params[] =
{
  {"2.0bad", },
  {"bad", },
  {"", },
  {"1e1000000", },
  {"-1e1000000" },
};

StaticParameterizedTest(StringDoublePair *string_value_pair, test_invalid_double_cast_params, type_hints,
                        test_invalid_double_cast)
{
  gdouble value;
  GError *error = NULL;

  cr_assert(not(type_cast_to_double(string_value_pair->string, -1, &value, &error)),
            "Type cast of invalid string (%s) to double should be failed", string_value_pair->string);
  cr_assert(not(zero(ptr, error)));
  cr_assert(eq(u32, error->domain, TYPE_HINTING_ERROR));
  cr_assert(eq(int, error->code, TYPE_HINTING_INVALID_CAST));
  g_clear_error(&error);
}

static StringUInt64Pair test_datetime_cast_params[] =
{
  {"12345",   12345000},
  {"12345.5", 12345500},
  {"12345.54", 12345540},
  {"12345.543", 12345543},
  {"12345.54321", 12345543},
  {"12345.987654", 12345987},
  {"12345.987654321", 12345987},
  {"12345+05:00", 12345000},
  {"12345-05:00", 12345000},
};

StaticParameterizedTest(StringUInt64Pair *string_value_pair, test_datetime_cast_params, type_hints, test_datetime_cast)
{
  gint64 value;
  GError *error = NULL;

  cr_assert(type_cast_to_datetime_msec(string_value_pair->string, -1, &value, &error),
            "Type cast of \"%s\" to msecs failed", string_value_pair->string);
  cr_assert(eq(i64, value, string_value_pair->value),
            "datetime cast failed %" G_GINT64_FORMAT " != %" G_GINT64_FORMAT,
            value, string_value_pair->value);
  cr_assert(zero(ptr, error));
}

static StringUInt64Pair test_invalid_datetime_cast_params[] =
{
  {"invalid", },
  {"12345T", },
  {"12345.", },
  {"12345.1234567890", },
  {"12345+XX:YY", 12345000},
  {"12345-05", 12345000},
  {"12345-XX:YY", 12345000}

};


StaticParameterizedTest(StringUInt64Pair *string_value_pair, test_invalid_datetime_cast_params, type_hints,
                        test_invalid_datetime_cast)
{
  GError *error = NULL;
  gint64 value;

  cr_assert(not(type_cast_to_datetime_msec(string_value_pair->string, -1, &value, &error)),
            "Type cast of invalid string to gint64 should have failed %s", string_value_pair->string);
  cr_assert(not(zero(ptr, error)));
  cr_assert(eq(u32, error->domain, TYPE_HINTING_ERROR));
  cr_assert(eq(int, error->code, TYPE_HINTING_INVALID_CAST));
  g_clear_error(&error);
}
