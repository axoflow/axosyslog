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

#include <criterion/criterion.h>

#include "libtest/mock-transport.h"

#include "apphook.h"
#include "transport/transport-factory-zlib.h"
#include "transport/transport-zlib.h"

#include <errno.h>
#include <string.h>
#include <zlib.h>

TestSuite(transport_zlib, .init = app_startup, .fini = app_shutdown);

/****************************************************************************
 * A base transport with a budget: it accepts @write_budget octets and answers
 * EAGAIN afterwards.  The mock transport of libtest never refuses a write, so
 * a blocked write and the propagation of a base cond need this one.
 ****************************************************************************/

typedef struct _BudgetTransport
{
  LogTransport super;
  GString *written;
  GString *readable;
  gsize read_pos;
  gsize write_budget;
  gsize read_budget;
  gboolean read_eof;
  LogTransportIOCond blocked_write_cond;
  LogTransportIOCond blocked_read_cond;
} BudgetTransport;

static gssize
_budget_write(LogTransport *s, const gpointer buf, gsize count)
{
  BudgetTransport *self = (BudgetTransport *) s;

  self->super.cond = LTIO_NOTHING;

  if (self->write_budget == 0)
    {
      self->super.cond = self->blocked_write_cond;
      errno = EAGAIN;
      return -1;
    }

  if (count > self->write_budget)
    count = self->write_budget;

  g_string_append_len(self->written, buf, count);
  self->write_budget -= count;

  return count;
}

static gssize
_budget_read(LogTransport *s, gpointer buf, gsize count, LogTransportAuxData *aux)
{
  BudgetTransport *self = (BudgetTransport *) s;
  gsize available = self->readable->len - self->read_pos;

  self->super.cond = LTIO_NOTHING;

  if (available > self->read_budget)
    available = self->read_budget;

  if (available == 0)
    {
      if (self->read_eof)
        return 0;
      self->super.cond = self->blocked_read_cond;
      errno = EAGAIN;
      return -1;
    }

  if (count > available)
    count = available;

  memcpy(buf, self->readable->str + self->read_pos, count);
  self->read_pos += count;
  self->read_budget -= count;

  return count;
}

static void
_budget_free(LogTransport *s)
{
  BudgetTransport *self = (BudgetTransport *) s;

  g_string_free(self->written, TRUE);
  g_string_free(self->readable, TRUE);
  log_transport_free_method(s);
}

static LogTransport *
_budget_transport_new(void)
{
  BudgetTransport *self = g_new0(BudgetTransport, 1);

  log_transport_init_instance(&self->super, "budget", -1);
  self->super.read = _budget_read;
  self->super.write = _budget_write;
  self->super.free_fn = _budget_free;
  self->written = g_string_new("");
  self->readable = g_string_new("");
  self->write_budget = G_MAXSIZE;
  self->read_budget = G_MAXSIZE;

  return &self->super;
}

/****************************************************************************
 * A fake TLS layer that counts everything passing through it, so a test can
 * tell which transport the zlib factory picked as its base.
 ****************************************************************************/

typedef struct _FakeTlsTransport
{
  LogTransport super;
  gsize written;
} FakeTlsTransport;

static gssize
_fake_tls_write(LogTransport *s, const gpointer buf, gsize count)
{
  FakeTlsTransport *self = (FakeTlsTransport *) s;
  LogTransport *base = log_transport_stack_get_transport(s->stack, LOG_TRANSPORT_INITIAL);

  self->written += count;
  return log_transport_write(base, buf, count);
}

static gssize
_fake_tls_read(LogTransport *s, gpointer buf, gsize count, LogTransportAuxData *aux)
{
  LogTransport *base = log_transport_stack_get_transport(s->stack, LOG_TRANSPORT_INITIAL);

  return log_transport_read(base, buf, count, aux);
}

static LogTransport *
_fake_tls_construct(const LogTransportFactory *s, LogTransportStack *stack)
{
  FakeTlsTransport *self = g_new0(FakeTlsTransport, 1);

  log_transport_init_instance(&self->super, "fake-tls", -1);
  self->super.read = _fake_tls_read;
  self->super.write = _fake_tls_write;

  return &self->super;
}

static LogTransportFactory *
_fake_tls_factory_new(void)
{
  LogTransportFactory *self = g_new0(LogTransportFactory, 1);

  log_transport_factory_init_instance(self, LOG_TRANSPORT_TLS);
  self->construct_transport = _fake_tls_construct;

  return self;
}

/****************************************************************************
 * One end of a compressed connection
 ****************************************************************************/

typedef struct _ZlibPeer
{
  LogTransportStack stack;
  LogTransport *base;
} ZlibPeer;

static void
_peer_init_with_base(ZlibPeer *self, LogTransport *base, gint level)
{
  memset(self, 0, sizeof(*self));
  self->base = base;

  log_transport_stack_init(&self->stack, base);
  /* the transports above have no fd of their own, and deinit would close
   * whatever the stack picked up from them */
  self->stack.fd = -1;

  log_transport_stack_add_factory(&self->stack, transport_factory_zlib_new(level));
  cr_assert(log_transport_stack_switch(&self->stack, LOG_TRANSPORT_ZLIB));
}

static void
_peer_init(ZlibPeer *self)
{
  _peer_init_with_base(self, _budget_transport_new(), Z_DEFAULT_COMPRESSION);
}

static void
_peer_deinit(ZlibPeer *self)
{
  log_transport_stack_deinit(&self->stack);
}

static BudgetTransport *
_peer_base(ZlibPeer *self)
{
  return (BudgetTransport *) self->base;
}

static gssize
_peer_write(ZlibPeer *self, const gchar *payload)
{
  return log_transport_stack_write(&self->stack, (gpointer) payload, strlen(payload));
}

static gssize
_peer_read(ZlibPeer *self, gchar *buf, gsize buflen)
{
  LogTransportAuxData aux;
  gssize rc;

  log_transport_aux_data_init(&aux);
  rc = log_transport_stack_read(&self->stack, buf, buflen, &aux);
  log_transport_aux_data_destroy(&aux);

  return rc;
}

static LogTransportIOCond
_peer_cond(ZlibPeer *self)
{
  return log_transport_stack_get_io_requirement(&self->stack);
}

static gboolean
_peer_has_pending_input(ZlibPeer *self)
{
  GIOCondition cond = 0;

  return log_transport_stack_poll_prepare(&self->stack, &cond);
}

static void
_transfer(ZlibPeer *sender, ZlibPeer *receiver)
{
  BudgetTransport *out = _peer_base(sender);
  BudgetTransport *in = _peer_base(receiver);

  g_string_append_len(in->readable, out->written->str, out->written->len);
  g_string_truncate(out->written, 0);
}

static GString *
_drain_reads(ZlibPeer *receiver, gsize read_size)
{
  GString *result = g_string_new("");
  gchar buf[65536];

  cr_assert_leq(read_size, sizeof(buf));

  while (TRUE)
    {
      gssize rc = _peer_read(receiver, buf, read_size);

      if (rc <= 0)
        break;
      g_string_append_len(result, buf, rc);
    }

  return result;
}

/* memmem() is a GNU extension, so @needle is searched for by hand */
static gboolean
_contains(const GString *haystack, const gchar *needle)
{
  gsize needle_len = strlen(needle);

  if (haystack->len < needle_len)
    return FALSE;

  for (gsize i = 0; i + needle_len <= haystack->len; i++)
    {
      if (memcmp(haystack->str + i, needle, needle_len) == 0)
        return TRUE;
    }

  return FALSE;
}

/* Inflate a whole zlib stream, so a test can assert what went on the wire. */
static GString *
_inflate_all(const GString *compressed)
{
  GString *result = g_string_new("");
  z_stream stream;
  guchar buf[65536];

  memset(&stream, 0, sizeof(stream));
  cr_assert_eq(inflateInit(&stream), Z_OK);

  stream.next_in = (Bytef *) compressed->str;
  stream.avail_in = compressed->len;

  while (TRUE)
    {
      stream.next_out = buf;
      stream.avail_out = sizeof(buf);

      gint rc = inflate(&stream, Z_SYNC_FLUSH);
      g_string_append_len(result, (const gchar *) buf, sizeof(buf) - stream.avail_out);

      if (rc == Z_STREAM_END || rc == Z_BUF_ERROR)
        break;
      cr_assert_eq(rc, Z_OK, "inflating the wire failed: %d", rc);
      if (stream.avail_in == 0 && stream.avail_out > 0)
        break;
    }

  inflateEnd(&stream);

  return result;
}

/****************************************************************************
 * Tests
 ****************************************************************************/

Test(transport_zlib, a_payload_written_by_one_adapter_is_read_back_by_another)
{
  const gchar *payload = "the quick brown fox jumps over the lazy dog\n";
  ZlibPeer sender, receiver;

  _peer_init(&sender);
  _peer_init(&receiver);

  cr_assert_eq(_peer_write(&sender, payload), (gssize) strlen(payload));

  GString *wire = _peer_base(&sender)->written;
  cr_assert_gt(wire->len, 0, "nothing reached the transport underneath");
  cr_assert_not(_contains(wire, payload), "the payload went on the wire uncompressed");

  _transfer(&sender, &receiver);

  GString *read_back = _drain_reads(&receiver, 4096);
  cr_assert_str_eq(read_back->str, payload);

  g_string_free(read_back, TRUE);
  _peer_deinit(&sender);
  _peer_deinit(&receiver);
}

Test(transport_zlib, every_write_ends_in_a_sync_flush_so_the_peer_decodes_it_at_once)
{
  ZlibPeer sender, receiver;

  _peer_init(&sender);
  _peer_init(&receiver);

  cr_assert_eq(_peer_write(&sender, "AAAA"), 4);
  _transfer(&sender, &receiver);

  GString *first = _drain_reads(&receiver, 4096);
  cr_assert_str_eq(first->str, "AAAA");

  cr_assert_eq(_peer_write(&sender, "BBBB"), 4);
  _transfer(&sender, &receiver);

  GString *second = _drain_reads(&receiver, 4096);
  cr_assert_str_eq(second->str, "BBBB");

  g_string_free(first, TRUE);
  g_string_free(second, TRUE);
  _peer_deinit(&sender);
  _peer_deinit(&receiver);
}

Test(transport_zlib, one_deflate_stream_spans_the_life_of_the_connection)
{
  ZlibPeer sender, receiver;

  _peer_init(&sender);
  _peer_init(&receiver);

  const gchar *payload = "a repeated line of an ALTP frame payload\n";

  cr_assert_eq(_peer_write(&sender, payload), (gssize) strlen(payload));
  gsize first_write = _peer_base(&sender)->written->len;
  _transfer(&sender, &receiver);

  cr_assert_eq(_peer_write(&sender, payload), (gssize) strlen(payload));
  gsize second_write = _peer_base(&sender)->written->len;
  _transfer(&sender, &receiver);

  cr_assert_lt(second_write, first_write, "the second write did not benefit from the history of the stream");

  GString *read_back = _drain_reads(&receiver, 4096);
  cr_assert_eq(read_back->len, 2 * strlen(payload));

  g_string_free(read_back, TRUE);
  _peer_deinit(&sender);
  _peer_deinit(&receiver);
}

Test(transport_zlib, a_short_write_of_the_base_transport_loses_nothing)
{
  ZlibPeer sender, receiver;
  LogTransport *mock = log_transport_mock_endless_records_new(LTM_EOF);

  _peer_init_with_base(&sender, mock, Z_DEFAULT_COMPRESSION);
  _peer_init(&receiver);

  log_transport_mock_set_write_chunk_limit((LogTransportMock *) mock, 1);

  const gchar *payload = "a payload that takes many writes to reach the peer\n";
  cr_assert_eq(_peer_write(&sender, payload), (gssize) strlen(payload));

  /* a deflate stream is full of NULs -- every sync flush ends in two of them
   * -- so a write buffer that copied it as a string would truncate it here */
  log_transport_mock_set_write_chunk_limit((LogTransportMock *) mock, 0);
  const gchar *second = "and a second one written in one go\n";
  cr_assert_eq(_peer_write(&sender, second), (gssize) strlen(second));

  gchar buf[65536];
  gssize len = log_transport_mock_read_from_write_buffer((LogTransportMock *) mock, buf, sizeof(buf));
  cr_assert_gt(len, 0);

  g_string_append_len(_peer_base(&receiver)->readable, buf, len);

  GString *read_back = _drain_reads(&receiver, 4096);
  cr_assert_str_eq(read_back->str, "a payload that takes many writes to reach the peer\n"
                                   "and a second one written in one go\n");

  g_string_free(read_back, TRUE);
  _peer_deinit(&sender);
  _peer_deinit(&receiver);
}

Test(transport_zlib, a_vector_write_is_one_record_identical_to_the_flat_write)
{
  ZlibPeer flat, vector;
  struct iovec iov[3] =
  {
    { .iov_base = "one ", .iov_len = 4 },
    { .iov_base = "two ", .iov_len = 4 },
    { .iov_base = "three\n", .iov_len = 6 },
  };

  _peer_init(&flat);
  _peer_init(&vector);

  cr_assert_eq(_peer_write(&flat, "one two three\n"), 14);
  cr_assert_eq(log_transport_stack_writev(&vector.stack, iov, 3), 14);

  cr_assert_eq(_peer_base(&vector)->written->len, _peer_base(&flat)->written->len);
  cr_assert_eq(memcmp(_peer_base(&vector)->written->str, _peer_base(&flat)->written->str,
                      _peer_base(&flat)->written->len), 0,
               "a vector must be deflated as one record, exactly like the flat write");

  _peer_deinit(&flat);
  _peer_deinit(&vector);
}

Test(transport_zlib, a_blocked_vector_write_is_retried_with_the_same_vector)
{
  ZlibPeer sender;
  struct iovec iov[2] =
  {
    { .iov_base = "AAAA", .iov_len = 4 },
    { .iov_base = "BBBB", .iov_len = 4 },
  };

  _peer_init(&sender);
  _peer_base(&sender)->write_budget = 0;

  cr_assert_eq(log_transport_stack_writev(&sender.stack, iov, 2), -1);
  cr_assert_eq(errno, EAGAIN);

  _peer_base(&sender)->write_budget = G_MAXSIZE;
  cr_assert_eq(log_transport_stack_writev(&sender.stack, iov, 2), 8);

  GString *plain = _inflate_all(_peer_base(&sender)->written);
  cr_assert_str_eq(plain->str, "AAAABBBB", "the blocked vector was deflated twice");

  g_string_free(plain, TRUE);
  _peer_deinit(&sender);
}

Test(transport_zlib, a_blocked_write_reports_eagain_and_asks_for_writability)
{
  ZlibPeer sender;

  _peer_init(&sender);
  _peer_base(&sender)->write_budget = 0;

  cr_assert_eq(_peer_write(&sender, "AAAA"), -1);
  cr_assert_eq(errno, EAGAIN);
  cr_assert_eq(_peer_cond(&sender), LTIO_WRITE_WANTS_WRITE);
  cr_assert_eq(_peer_base(&sender)->written->len, 0);

  _peer_deinit(&sender);
}

Test(transport_zlib, a_blocked_write_is_not_compressed_a_second_time_by_the_retry)
{
  ZlibPeer sender;

  _peer_init(&sender);
  _peer_base(&sender)->write_budget = 0;

  cr_assert_eq(_peer_write(&sender, "AAAA"), -1);
  cr_assert_eq(errno, EAGAIN);

  /* the retry contract: the caller repeats the blocked prefix octet for octet,
   * and the adapter must not deflate that prefix a second time */
  _peer_base(&sender)->write_budget = G_MAXSIZE;
  cr_assert_eq(_peer_write(&sender, "AAAA"), 4);

  GString *plain = _inflate_all(_peer_base(&sender)->written);
  cr_assert_str_eq(plain->str, "AAAA", "the blocked prefix was deflated twice");

  g_string_free(plain, TRUE);
  _peer_deinit(&sender);
}

Test(transport_zlib, a_blocked_write_may_be_retried_with_more_data_appended)
{
  ZlibPeer sender;

  _peer_init(&sender);
  _peer_base(&sender)->write_budget = 0;

  cr_assert_eq(_peer_write(&sender, "AAAA"), -1);
  cr_assert_eq(errno, EAGAIN);

  _peer_base(&sender)->write_budget = G_MAXSIZE;
  cr_assert_eq(_peer_write(&sender, "AAAABBBB"), 8);

  GString *plain = _inflate_all(_peer_base(&sender)->written);
  cr_assert_str_eq(plain->str, "AAAABBBB");

  g_string_free(plain, TRUE);
  _peer_deinit(&sender);
}

Test(transport_zlib, a_retry_that_drops_the_blocked_prefix_is_an_error)
{
  ZlibPeer sender;

  _peer_init(&sender);
  _peer_base(&sender)->write_budget = 0;

  cr_assert_eq(_peer_write(&sender, "AAAA"), -1);
  cr_assert_eq(errno, EAGAIN);

  _peer_base(&sender)->write_budget = G_MAXSIZE;
  cr_assert_eq(_peer_write(&sender, "AA"), -1);
  cr_assert_eq(errno, EINVAL);

  _peer_deinit(&sender);
}

Test(transport_zlib, a_reader_with_a_small_buffer_gets_the_rest_on_its_next_reads)
{
  ZlibPeer sender, receiver;
  GString *payload = g_string_new("");

  _peer_init(&sender);
  _peer_init(&receiver);

  for (gint i = 0; i < 512; i++)
    g_string_append_printf(payload, "%03d a line that inflates to far more than a single read takes\n", i);

  cr_assert_eq(log_transport_stack_write(&sender.stack, payload->str, payload->len), (gssize) payload->len);
  _transfer(&sender, &receiver);

  /* the octets left inside the adapter are invisible to a poll of the fd, so
   * poll_prepare() has to report them or the reader stalls */
  gchar buf[64];
  cr_assert_eq(_peer_read(&receiver, buf, sizeof(buf)), (gssize) sizeof(buf));
  cr_assert(_peer_has_pending_input(&receiver), "the adapter did not report the input it still holds");

  GString *read_back = g_string_new_len(buf, sizeof(buf));
  GString *rest = _drain_reads(&receiver, 64);
  g_string_append_len(read_back, rest->str, rest->len);

  cr_assert_eq(read_back->len, payload->len);
  cr_assert_arr_eq(read_back->str, payload->str, payload->len);
  cr_assert_not(_peer_has_pending_input(&receiver), "the adapter still reports input after it was all delivered");

  g_string_free(rest, TRUE);
  g_string_free(read_back, TRUE);
  g_string_free(payload, TRUE);
  _peer_deinit(&sender);
  _peer_deinit(&receiver);
}

Test(transport_zlib, the_end_of_input_of_the_base_transport_is_reported_once_everything_is_inflated)
{
  ZlibPeer sender, receiver;

  _peer_init(&sender);
  _peer_init(&receiver);

  cr_assert_eq(_peer_write(&sender, "AAAA"), 4);
  _transfer(&sender, &receiver);
  _peer_base(&receiver)->read_eof = TRUE;

  gchar buf[1024];
  cr_assert_eq(_peer_read(&receiver, buf, sizeof(buf)), 4);
  cr_assert_arr_eq(buf, "AAAA", 4);

  cr_assert_eq(_peer_read(&receiver, buf, sizeof(buf)), 0);

  _peer_deinit(&sender);
  _peer_deinit(&receiver);
}

Test(transport_zlib, a_corrupted_stream_is_an_error_and_not_a_silent_truncation)
{
  ZlibPeer receiver;

  _peer_init(&receiver);
  g_string_append(_peer_base(&receiver)->readable, "not a zlib stream at all, not even close");

  gchar buf[1024];
  cr_assert_eq(_peer_read(&receiver, buf, sizeof(buf)), -1);
  cr_assert_eq(errno, EPROTO);

  _peer_deinit(&receiver);
}

Test(transport_zlib, the_cond_of_the_base_transport_is_propagated)
{
  ZlibPeer receiver;

  _peer_init(&receiver);

  /* a TLS renegotiation in the middle of a read wants writability */
  _peer_base(&receiver)->blocked_read_cond = LTIO_READ_WANTS_WRITE;

  gchar buf[1024];
  cr_assert_eq(_peer_read(&receiver, buf, sizeof(buf)), -1);
  cr_assert_eq(errno, EAGAIN);
  cr_assert_eq(_peer_cond(&receiver), LTIO_READ_WANTS_WRITE);

  _peer_base(&receiver)->write_budget = 0;
  _peer_base(&receiver)->blocked_write_cond = LTIO_WRITE_WANTS_READ;

  cr_assert_eq(_peer_write(&receiver, "AAAA"), -1);
  cr_assert_eq(errno, EAGAIN);
  cr_assert_eq(_peer_cond(&receiver), LTIO_WRITE_WANTS_READ);

  _peer_deinit(&receiver);
}

Test(transport_zlib, the_factory_wraps_whichever_transport_is_active)
{
  LogTransportStack stack;
  LogTransport *base = _budget_transport_new();

  log_transport_stack_init(&stack, base);
  stack.fd = -1;

  log_transport_stack_add_factory(&stack, _fake_tls_factory_new());
  cr_assert(log_transport_stack_switch(&stack, LOG_TRANSPORT_TLS));

  log_transport_stack_add_factory(&stack, transport_factory_zlib_new(Z_DEFAULT_COMPRESSION));
  cr_assert(log_transport_stack_switch(&stack, LOG_TRANSPORT_ZLIB));

  cr_assert_eq(log_transport_stack_write(&stack, (gpointer) "AAAA", 4), 4);

  FakeTlsTransport *tls = (FakeTlsTransport *) log_transport_stack_get_transport(&stack, LOG_TRANSPORT_TLS);
  cr_assert_gt(tls->written, 0, "the compressed octets did not go through the TLS layer");
  cr_assert_eq(((BudgetTransport *) base)->written->len, tls->written);

  log_transport_stack_deinit(&stack);
}
