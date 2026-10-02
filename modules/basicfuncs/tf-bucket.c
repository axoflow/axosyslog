/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

/*
 * $(bucket <bucket-count> value...)
 * $(bucket --weights W1,W2,... value...)
 *
 * Hashes the concatenation of its value arguments and returns a bucket
 * index in the range [0, bucket-count) (or [0, number-of-weights) in the
 * --weights form). The mapping from a given set of values to a bucket
 * index is deterministic within a running configuration, so it can be used
 * to consistently route a message down one of several log paths, e.g.
 * based on $HOST.
 *
 * Options:
 *   --weights W1,W2,...  Comma separated list of non-negative integer
 *                         weights, one per bucket, replacing the plain
 *                         bucket-count argument. A bucket's share of the
 *                         input is proportional to its weight; a weight of
 *                         zero drains a bucket entirely.
 *
 * NOTE: the hash function used is not part of any documented contract: the
 * mapping from values to bucket indexes may change across syslog-ng
 * versions or platforms, so it must not be relied upon to stay stable
 * across upgrades.
 *
 * The current hash is the DJB2 recurrence (the same one g_str_hash() uses)
 * computed over an explicit length, so embedded NUL bytes take part, followed
 * by the MurmurHash3 fmix32 finalizer. DJB2 alone is a poor shard key: its
 * multiplier 33 is congruent to 1 modulo 32, so for bucket counts 2, 4, 8, 16
 * and 32 the raw hash modulo the count degenerates to the byte sum modulo the
 * count, making anagrams collide and neighbouring hostnames go round-robin.
 * The finalizer is a bijection on 32 bits that mixes every input bit into
 * the low bits, which removes that structure at the cost of five operations.
 */

/* upper bound on the bucket count, the number of weights and each
 * individual weight: keeps the weight sum well within guint range */
#define TF_BUCKET_MAX_BUCKETS 4096

typedef struct _TFBucketState
{
  TFSimpleFuncState super;
  guint num_buckets;
  guint *weights;         /* NULL for the plain (uniform bucket-count) form */
  guint weights_sum;
} TFBucketState;

static gboolean
_tf_bucket_parse_weights(const gchar *weights_str, guint **out_weights, guint *out_num_buckets,
                         guint *out_weights_sum, GError **error)
{
  gchar **tokens = g_strsplit(weights_str, ",", 0);
  guint n = g_strv_length(tokens);

  if (n == 0)
    {
      g_set_error(error, LOG_TEMPLATE_ERROR, LOG_TEMPLATE_ERROR_COMPILE,
                  "$(bucket) --weights requires a comma separated list of at least one weight");
      g_strfreev(tokens);
      return FALSE;
    }

  if (n > TF_BUCKET_MAX_BUCKETS)
    {
      g_set_error(error, LOG_TEMPLATE_ERROR, LOG_TEMPLATE_ERROR_COMPILE,
                  "$(bucket) --weights accepts at most %d weights, got %u", TF_BUCKET_MAX_BUCKETS, n);
      g_strfreev(tokens);
      return FALSE;
    }

  guint *weights = g_new0(guint, n);
  guint sum = 0;
  gboolean success = TRUE;

  for (guint i = 0; i < n; i++)
    {
      gint64 w;

      if (!parse_int64(tokens[i], &w) || w < 0 || w > TF_BUCKET_MAX_BUCKETS)
        {
          g_set_error(error, LOG_TEMPLATE_ERROR, LOG_TEMPLATE_ERROR_COMPILE,
                      "$(bucket) --weights must be a comma separated list of non-negative integers "
                      "not greater than %d, got \"%s\"", TF_BUCKET_MAX_BUCKETS, tokens[i]);
          success = FALSE;
          break;
        }
      weights[i] = (guint) w;
      sum += weights[i];
    }
  g_strfreev(tokens);

  if (success && sum == 0)
    {
      g_set_error(error, LOG_TEMPLATE_ERROR, LOG_TEMPLATE_ERROR_COMPILE,
                  "$(bucket) --weights must not all be zero");
      success = FALSE;
    }

  if (!success)
    {
      g_free(weights);
      return FALSE;
    }

  *out_weights = weights;
  *out_num_buckets = n;
  *out_weights_sum = sum;
  return TRUE;
}

static gboolean
tf_bucket_prepare(LogTemplateFunction *self, gpointer s, LogTemplate *parent, gint argc, gchar *argv[],
                  GError **error)
{
  TFBucketState *state = (TFBucketState *) s;
  GOptionContext *ctx;
  gchar *weights_str = NULL;
  GOptionEntry bucket_options[] =
  {
    { "weights", 0, 0, G_OPTION_ARG_STRING, &weights_str, NULL, NULL },
    { NULL }
  };

  ctx = g_option_context_new("bucket");
  g_option_context_set_ignore_unknown_options(ctx, FALSE);
  g_option_context_add_main_entries(ctx, bucket_options, NULL);

  if (!g_option_context_parse(ctx, &argc, &argv, error))
    {
      g_option_context_free(ctx);
      g_free(weights_str);
      return FALSE;
    }
  g_option_context_free(ctx);

  if (weights_str)
    {
      gboolean success = _tf_bucket_parse_weights(weights_str, &state->weights, &state->num_buckets,
                                                  &state->weights_sum, error);
      g_free(weights_str);
      if (!success)
        return FALSE;

      if (argc < 2)
        {
          g_set_error(error, LOG_TEMPLATE_ERROR, LOG_TEMPLATE_ERROR_COMPILE,
                      "$(bucket) requires at least one value argument");
          g_free(state->weights);
          state->weights = NULL;
          return FALSE;
        }

      return tf_simple_func_prepare(self, state, parent, argc, argv, error);
    }

  if (argc < 3)
    {
      g_set_error(error, LOG_TEMPLATE_ERROR, LOG_TEMPLATE_ERROR_COMPILE,
                  "$(bucket) requires a bucket count and at least one value argument, "
                  "usage: $(bucket <bucket-count> value...)");
      return FALSE;
    }

  gint64 count;
  if (!parse_int64(argv[1], &count) || count < 1 || count > TF_BUCKET_MAX_BUCKETS)
    {
      g_set_error(error, LOG_TEMPLATE_ERROR, LOG_TEMPLATE_ERROR_COMPILE,
                  "$(bucket) bucket count must be a positive integer not greater than %d, got \"%s\"",
                  TF_BUCKET_MAX_BUCKETS, argv[1]);
      return FALSE;
    }
  state->num_buckets = (guint) count;
  state->weights = NULL;

  /* drop the bucket-count positional argument (argv[1]) before handing the
   * remaining values over to be compiled as templates */
  gchar **value_argv = g_new(gchar *, argc - 1);
  value_argv[0] = argv[0];
  for (gint i = 2; i < argc; i++)
    value_argv[i - 1] = argv[i];

  gboolean success = tf_simple_func_prepare(self, state, parent, argc - 1, value_argv, error);
  g_free(value_argv);
  return success;
}

static inline guint32
_tf_bucket_fmix32(guint32 h)
{
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;
  return h;
}

static guint32
_tf_bucket_hash(const gchar *data, gsize len)
{
  guint32 h = 5381;

  for (gsize i = 0; i < len; i++)
    h = (h << 5) + h + (guchar) data[i];

  return _tf_bucket_fmix32(h);
}

static guint32
_tf_bucket_hash_combine(gint argc, GString *argv[])
{
  GString *buf = scratch_buffers_alloc();

  for (gint i = 0; i < argc; i++)
    {
      /* separate values so ("ab","c") and ("a","bc") don't collide */
      if (i > 0)
        g_string_append_c(buf, '\x1e');
      g_string_append_len(buf, argv[i]->str, argv[i]->len);
    }
  return _tf_bucket_hash(buf->str, buf->len);
}

static guint
_tf_bucket_select(TFBucketState *state, guint hash)
{
  if (!state->weights)
    return hash % state->num_buckets;

  guint slot = hash % state->weights_sum;
  guint cumulative = 0;

  for (guint i = 0; i < state->num_buckets; i++)
    {
      cumulative += state->weights[i];
      if (slot < cumulative)
        return i;
    }
  g_assert_not_reached();
}

static void
tf_bucket_call(LogTemplateFunction *self, gpointer s, const LogTemplateInvokeArgs *args, GString *result,
               LogMessageValueType *type)
{
  TFBucketState *state = (TFBucketState *) s;
  guint hash = _tf_bucket_hash_combine(state->super.argc, (GString **) args->argv);
  guint bucket = _tf_bucket_select(state, hash);

  *type = LM_VT_INTEGER;
  g_string_append_printf(result, "%u", bucket);
}

static void
tf_bucket_free_state(gpointer s)
{
  TFBucketState *state = (TFBucketState *) s;

  g_free(state->weights);
  tf_simple_func_free_state(&state->super);
}

TEMPLATE_FUNCTION(TFBucketState, tf_bucket, tf_bucket_prepare, tf_simple_func_eval, tf_bucket_call,
                  tf_bucket_free_state, NULL);
