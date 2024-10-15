/*
 * Copyright (c) 2024 Balázs Scheidler <balazs.scheidler@axoflow.com>
 * Copyright (c) 2024 Axoflow
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
#include "stackdump.h"
#include "console.h"

#include <execinfo.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef __linux__

/* this is Linux only for now */

static void
_stackdump_print_stack(gpointer stack_pointer)
{
  guint8 *p = stack_pointer;

  for (gint i = 0; i < 16; i++)
    {
      gchar line[51] = {0};
      for (gint j = 0; j < 8; j++)
        {
          sprintf(&line[j*3], "%02x ", (guint) *(p+j));
        }
      line[8*3] = ' ';
      for (gint j = 8; j < 16; j++)
        {
          sprintf(&line[j*3 + 1], "%02x ", (guint) *(p+j));
        }

      console_printf("Stack %p: %s", p, line);
      p += 16;
    }
}

/* should not do any allocation to allow this to work even if our heap is corrupted */
static void
_stackdump_print_maps(void)
{
  int fd;

  console_printf("Maps file follows");
  fd = open("/proc/self/maps", O_RDONLY);
  if (fd < 0)
    {
      console_printf("Error opening /proc/self/maps");
      return;
    }

  gchar buf[1024];
  int rc;
  gchar *p, *eol;
  gint avail, end = 0;

  while (1)
    {
      avail = sizeof(buf) - end;
      rc = read(fd, buf + end, avail);

      if (rc < 0)
        break;

      end += rc;

      if (rc == 0)
        break;

      p = buf;
      while (*p && p < (buf + end))
        {
          eol = memchr(p, '\n', buf + end - p);
          if (eol)
            {
              *eol = 0;
              console_printf("%s", p);
              p = eol + 1;
            }
          else
            {
              end = end - (p - buf);
              memmove(buf, p, end);
              break;
            }
        }
    }
  if (end)
    console_printf("%.*s", end, buf);
  close(fd);
}

static inline void
_stackdump_print_backtrace(void)
{
  void *bt[256];
  gint count;

  count = backtrace(bt, 256);
  console_printf("Raw backtrace dump, count=%d", count);
  for (gint i = 0; i < count; i++)
    {
      console_printf("[%d]: %p", i, bt[i]);
    }
  if (count)
    {
      gchar **symbols;

      console_printf("Symbol backtrace dump, count=%d", count);
      symbols = backtrace_symbols(bt, count);
      for (gint i = 0; i < count; i++)
        {
          console_printf("[%d]: %s", i, symbols[i]);
        }
    }
}


#ifdef __x86_64__
/****************************************************************************
 *
 *
 * x86_64 support
 *
 *
 ****************************************************************************/

void
_stackdump_print_registers(struct sigcontext *p)
{
  console_printf(
        "Registers: "
        "rax=%016lx rbx=%016lx rcx=%016lx rdx=%016lx rsi=%016lx rdi=%016lx "
        "rbp=%016lx rsp=%016lx r8=%016lx r9=%016lx r10=%016lx r11=%016lx "
        "r12=%016lx r13=%016lx r14=%016lx r15=%016lx rip=%016lx",
        p->rax, p->rbx, p->rcx, p->rdx, p->rsi, p->rdi, p->rbp, p->rsp, p->r8, p->r9, p->r10, p->r11, p->r12, p->r13, p->r14, p->r15, p->rip);
  _stackdump_print_stack((gpointer) p->rsp);
}

#elif __x86__
/****************************************************************************
 *
 *
 * i386 support
 *
 *
 ****************************************************************************/

void
_stackdump_print_registers(struct sigcontext *p)
{
  console_printf(
        "Registers: eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx eip=%08lx",
        p->eax, p->ebx, p->ecx, p->edx, p->esi, p->edi, p->ebp, p->esp, p->eip);
  _stackdump_print_stack((gpointer) p->esp);
}

#else
/****************************************************************************
 *
 *
 * unsupported platform
 *
 *
 ****************************************************************************/

static void
_stackdump_print_registers(struct sigcontext *p)
{
}

#endif


static inline void
_stackdump_log(struct sigcontext *p)
{
  /* the order is important here, even if it might be illogical.  The
   * backtrace function is the most fragile (as backtrace_symbols() may
   * allocate memory).  Let's log everything else first, and then we can
   * produce the backtrace, which is potentially causing another crash.  */

  _stackdump_print_registers(p);
  _stackdump_print_maps();
  _stackdump_print_backtrace();
}

static void
_fatal_signal_handler(int signo, siginfo_t *info, void *uc)
{
  struct ucontext_t *ucontext = (struct ucontext_t *) uc;
  struct sigcontext *p = (struct sigcontext *) &ucontext->uc_mcontext;
  struct sigaction act;

  memset(&act, 0, sizeof(act));
  act.sa_handler = SIG_DFL;
  sigaction(signo, &act, NULL);

  console_printf("Fatal signal received, stackdump follows, signal=%d", signo);
  _stackdump_log(p);
  /* let's get a stacktrace as well */
  kill(getpid(), signo);
}

void
stackdump_setup_signal(gint signal_number)
{
  struct sigaction act;

  memset(&act, 0, sizeof(act));
  act.sa_flags = SA_SIGINFO;
  act.sa_sigaction = _fatal_signal_handler;

  sigaction(signal_number, &act, NULL);
}

#else

void
stackdump_setup_signal(gint signal_number)
{
}

#endif
