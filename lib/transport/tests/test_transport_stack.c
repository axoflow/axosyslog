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

#include "transport/transport-stack.h"
#include "apphook.h"

#define DEFINE_TEST_TRANSPORT_WITH_FACTORY(TypePrefix, FunPrefix) \
  typedef struct TypePrefix ## Transport_ TypePrefix ## Transport; \
  typedef struct TypePrefix ## TransportFactory_ TypePrefix ## TransportFactory; \
  struct TypePrefix ## Transport_ \
  { \
    LogTransport super; \
  }; \
  static gssize \
  FunPrefix ## _read(LogTransport *s, gpointer buf, gsize count, LogTransportAuxData *aux) \
  { \
    return count; \
  }\
  static gssize \
  FunPrefix ## _write(LogTransport *s, const gpointer buf, gsize count) \
  { \
    return count; \
  }\
  LogTransport * \
  FunPrefix ## _transport_new(gint fd) \
  { \
    TypePrefix ## Transport *self = g_new0(TypePrefix ## Transport, 1); \
    log_transport_init_instance(&self->super, # FunPrefix, -1); \
    self->super.read = FunPrefix ## _read; \
    self->super.write = FunPrefix ## _write; \
    self->super.fd = fd; \
    return &self->super; \
  } \
  struct TypePrefix ## TransportFactory_\
  { \
    LogTransportFactory super; \
  }; \
  static LogTransport * \
  FunPrefix ## _factory_construct(const LogTransportFactory *s, LogTransportStack *stack) \
  {\
    LogTransport *transport = FunPrefix ## _transport_new(stack->fd); \
    return transport; \
  } \
  LogTransportFactory * \
  FunPrefix ## _transport_factory_new(void) \
  { \
    TypePrefix ## TransportFactory *self = g_new0(TypePrefix ## TransportFactory, 1); \
    log_transport_factory_init_instance(&self->super, LOG_TRANSPORT_TLS); \
    self->super.construct_transport = FunPrefix ## _factory_construct; \
    return &self->super; \
  } \

TestSuite(transport_stack, .init = app_startup, .fini = app_shutdown);

DEFINE_TEST_TRANSPORT_WITH_FACTORY(Fake, fake);
DEFINE_TEST_TRANSPORT_WITH_FACTORY(Default, default);

Test(transport_stack, test_switch_transport)
{
  LogTransportFactory *fake_factory = fake_transport_factory_new();
  gint fd = -2;

  LogTransportStack stack;
  log_transport_stack_init(&stack, default_transport_new(fd));

  cr_assert(stack.fd == fd);

  LogTransport *active_transport = log_transport_stack_get_active(&stack);

  cr_expect_eq(active_transport->read, default_read);
  cr_expect_eq(active_transport->write, default_write);
  cr_expect_str_eq(active_transport->name, "default");

  log_transport_stack_add_factory(&stack, fake_factory);

  cr_expect(log_transport_stack_switch(&stack, LOG_TRANSPORT_TLS));
  active_transport = log_transport_stack_get_active(&stack);

  cr_expect_eq(active_transport->read, fake_read);
  cr_expect_eq(active_transport->write, fake_write);
  cr_expect_str_eq(active_transport->name, "fake");

  log_transport_stack_deinit(&stack);
}
