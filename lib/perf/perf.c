#include "perf.h"
#include "messages.h"
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>


typedef struct _PerfTrampolineArea
{
  guint8 *area;
  gsize size;
  gsize code_size;
  struct
  {
    gint num;
    gint alloc;
  } trampolines;
} PerfTrampolineArea;

extern void _perf_trampoline_func_start(void);
extern void _perf_trampoline_func_end(void);

#define PAGE_SIZE 4096
#define PAGE_MAX_INDEX ((PAGE_SIZE-1))

#define SIZE_TO_PAGES(s) (((s + PAGE_MAX_INDEX) & ~PAGE_MAX_INDEX) / PAGE_SIZE)
#define PAGE_TO_SIZE(p)  (p * PAGE_SIZE)

#define MAX_TRAMPOLINES 16384

static gboolean
_allocate_trampoline_area(PerfTrampolineArea *self)
{
  guint8 *start = (guint8 *) &_perf_trampoline_func_start;
  guint8 *end = (guint8 *) &_perf_trampoline_func_end;

  self->code_size = end - start;
  gsize code_pages = SIZE_TO_PAGES(self->code_size * MAX_TRAMPOLINES);

  self->size = PAGE_TO_SIZE(code_pages);
  self->area = mmap(NULL,
                    self->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                    -1, 0);
  if (self->area == MAP_FAILED)
    return FALSE;

  self->trampolines.num = self->size / self->code_size;
  self->trampolines.alloc = 0;

  for (gint i = 0; i < self->trampolines.num; i++)
    {
      memcpy(self->area + i * self->code_size, start, self->code_size * sizeof(gchar));
    }
  return TRUE;
}

static gpointer
_generate_trampoline(PerfTrampolineArea *self, gpointer target_address)
{
  if (self->trampolines.alloc >= self->trampolines.num)
    return NULL;

  gint res = mprotect(self->area, self->size, PROT_READ | PROT_WRITE);
  if (res < 0)
    return NULL;

  guint8 *trampoline_start = self->area + (self->trampolines.alloc++) * self->code_size;
  guint8 *trampoline_end = trampoline_start + self->code_size;
  uintptr_t *value_p = (uintptr_t *) (trampoline_end - sizeof(target_address));
  *value_p = (uintptr_t) target_address;

  __builtin___clear_cache(trampoline_start, trampoline_end);

  res = mprotect(self->area, self->size, PROT_READ | PROT_EXEC);
  if (res < 0)
    return NULL;
  return (gpointer) trampoline_start;
}

static gboolean
_is_trampoline_address(PerfTrampolineArea *self, guint8 *address)
{
  return self->area <= address && self->area + self->size > address;
}

static gboolean
_save_symbol(gpointer address, gsize size, const gchar *symbol_name)
{
  gchar filename[64];

  g_snprintf(filename, sizeof(filename), "/tmp/perf-%d.map", (int) getpid());
  FILE *mapfile = fopen(filename, "a");
  if (!mapfile)
    return FALSE;
  fprintf(mapfile, "%0lx %ld %s\n", (uintptr_t) address, size, symbol_name);
  fclose(mapfile);
  return TRUE;
}

static PerfTrampolineArea trampolines;

gpointer
perf_generate_trampoline(gpointer target_address, const gchar *symbol_name)
{
  gpointer t = _generate_trampoline(&trampolines, target_address);

  if (!t)
    {
      msg_warning_once("WARNING: out of free trampoline slots",
                       evt_tag_int("max_trampolines", trampolines.trampolines.num));
      return target_address;
    }

  _save_symbol(t, trampolines.code_size, symbol_name);
  return t;
}

gboolean
perf_is_trampoline_address(gpointer address)
{
  return _is_trampoline_address(&trampolines, address);
}

void
perf_global_init(void)
{
  _allocate_trampoline_area(&trampolines);
}

void
perf_global_deinit(void)
{
  /* we are never freeing the trampolines, as that would invalidate any
   * function pointers that point into that area */
}
