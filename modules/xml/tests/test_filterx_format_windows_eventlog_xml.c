/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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
 */

#include <criterion/criterion.h>

#include "filterx-func-format-windows-eventlog-xml.h"
#include "filterx-parse-windows-eventlog-xml.h"
#include "filterx/object-string.h"
#include "filterx/filterx-eval.h"
#include "scratch-buffers.h"
#include "apphook.h"
#include "cfg.h"

#include "libtest/filterx-lib.h"

#define EVENT_HEAD "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" \
  "<System><EventID>999</EventID></System>"

static FilterXObject *
_eval_func(FilterXExpr *(*ctor)(FilterXFunctionArgs *, GError **), FilterXObject *input)
{
  FilterXFunctionArg *arg = filterx_function_arg_new(NULL, filterx_object_expr_new(input));
  GList *args_list = g_list_append(NULL, arg);
  GError *error = NULL;
  FilterXFunctionArgs *args = filterx_function_args_new(args_list, &error);
  g_assert(!error);

  FilterXExpr *func = ctor(args, &error);
  g_assert(!error);

  FilterXObject *result = init_and_eval_expr(func);
  cr_assert(result);
  cr_assert(filterx_eval_get_error_count() == 0);

  filterx_expr_unref(func);
  return result;
}

static FilterXObject *
_parse(const gchar *xml)
{
  return _eval_func(filterx_function_parse_windows_eventlog_xml_new, filterx_string_new(xml, -1));
}

static void
_assert_format(const gchar *xml, const gchar *expected_xml)
{
  FilterXObject *dict = _parse(xml);
  FilterXObject *formatted = _eval_func(filterx_function_format_windows_eventlog_xml_new, filterx_object_ref(dict));

  GString *formatted_str = g_string_new(NULL);
  cr_assert(filterx_object_str(formatted, formatted_str));
  cr_assert_str_eq(formatted_str->str, expected_xml);

  FilterXObject *reparsed = _parse(formatted_str->str);
  GString *dict_repr = g_string_new(NULL);
  GString *reparsed_repr = g_string_new(NULL);
  filterx_object_repr(dict, dict_repr);
  filterx_object_repr(reparsed, reparsed_repr);
  cr_assert_str_eq(reparsed_repr->str, dict_repr->str);

  g_string_free(reparsed_repr, TRUE);
  g_string_free(dict_repr, TRUE);
  g_string_free(formatted_str, TRUE);
  filterx_object_unref(reparsed);
  filterx_object_unref(formatted);
  filterx_object_unref(dict);
}

Test(filterx_format_windows_eventlog_xml, leaf_as_last_child_of_element_with_attribute)
{
  _assert_format(EVENT_HEAD "<RenderingInfo Culture='en-US'><Keywords></Keywords></RenderingInfo></Event>",
                 EVENT_HEAD "<RenderingInfo Culture='en-US'><Keywords/></RenderingInfo></Event>");

  _assert_format(EVENT_HEAD "<RenderingInfo Culture='en-US'><Keywords></Keywords><Zzz>tail</Zzz></RenderingInfo></Event>",
                 EVENT_HEAD "<RenderingInfo Culture='en-US'><Keywords/><Zzz>tail</Zzz></RenderingInfo></Event>");
}

Test(filterx_format_windows_eventlog_xml, schannel_36880)
{
  _assert_format("<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>"
                 "<System><Provider Name='Schannel' Guid='{1f678132-5938-4686-9fdc-c8ff68f15c85}'/>"
                 "<EventID>36880</EventID><Version>0</Version><Level>4</Level><Task>0</Task><Opcode>0</Opcode>"
                 "<Keywords>0x8000000000000000</Keywords><TimeCreated SystemTime='2026-09-21T10:00:00.0000000Z'/>"
                 "<EventRecordID>68584070</EventRecordID><Correlation/><Execution ProcessID='920' ThreadID='3296'/>"
                 "<Channel>System</Channel><Computer>acme-dc1v.example.com</Computer><Security UserID='S-1-5-18'/>"
                 "</System>"
                 "<UserData><EventXML xmlns='LSA_NS'><Type>server</Type><Protocol>TLS 1.2</Protocol>"
                 "<CipherSuite>0xc030</CipherSuite><ExchangeStrength>384</ExchangeStrength>"
                 "<ContextHandle>0x0</ContextHandle><TargetName></TargetName>"
                 "<LocalCertSubjectName>CN=acme-dc1v.example.com</LocalCertSubjectName>"
                 "<RemoteCertSubjectName></RemoteCertSubjectName></EventXML></UserData>"
                 "<RenderingInfo Culture='en-US'>"
                 "<Message>A TLS server handshake completed successfully. The negotiated cryptographic parameters are as follows.\n"
                 "\n"
                 "   Protocol version: TLS 1.2\n"
                 "   CipherSuite: 0xC030\n"
                 "   Exchange strength: 384 bits\n"
                 "   Context handle: 0x0\n"
                 "   Target name: \n"
                 "   Local certificate subject name: CN=acme-dc1v.example.com\n"
                 "   Remote certificate subject name: </Message>"
                 "<Level>Information</Level><Task></Task><Opcode>Info</Opcode><Channel>System</Channel>"
                 "<Provider></Provider><Keywords></Keywords></RenderingInfo></Event>",

                 "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>"
                 "<System><Provider Name='Schannel' Guid='{1f678132-5938-4686-9fdc-c8ff68f15c85}'/>"
                 "<EventID>36880</EventID><Version>0</Version><Level>4</Level><Task>0</Task><Opcode>0</Opcode>"
                 "<Keywords>0x8000000000000000</Keywords><TimeCreated SystemTime='2026-09-21T10:00:00.0000000Z'/>"
                 "<EventRecordID>68584070</EventRecordID><Correlation/><Execution ProcessID='920' ThreadID='3296'/>"
                 "<Channel>System</Channel><Computer>acme-dc1v.example.com</Computer><Security UserID='S-1-5-18'/>"
                 "</System>"
                 "<UserData><EventXML xmlns='LSA_NS'><Type>server</Type><Protocol>TLS 1.2</Protocol>"
                 "<CipherSuite>0xc030</CipherSuite><ExchangeStrength>384</ExchangeStrength>"
                 "<ContextHandle>0x0</ContextHandle><TargetName/>"
                 "<LocalCertSubjectName>CN=acme-dc1v.example.com</LocalCertSubjectName>"
                 "<RemoteCertSubjectName/></EventXML></UserData>"
                 "<RenderingInfo Culture='en-US'>"
                 "<Message>A TLS server handshake completed successfully. The negotiated cryptographic parameters are as follows.\n"
                 "\n"
                 "   Protocol version: TLS 1.2\n"
                 "   CipherSuite: 0xC030\n"
                 "   Exchange strength: 384 bits\n"
                 "   Context handle: 0x0\n"
                 "   Target name: \n"
                 "   Local certificate subject name: CN=acme-dc1v.example.com\n"
                 "   Remote certificate subject name:</Message>"
                 "<Level>Information</Level><Task/><Opcode>Info</Opcode><Channel>System</Channel>"
                 "<Provider/><Keywords/></RenderingInfo></Event>");
}

static void
setup(void)
{
  configuration = cfg_new_snippet();
  app_startup();
  init_libtest_filterx();
}

static void
teardown(void)
{
  scratch_buffers_explicit_gc();
  deinit_libtest_filterx();
  app_shutdown();
  cfg_free(configuration);
}

TestSuite(filterx_format_windows_eventlog_xml, .init = setup, .fini = teardown);
