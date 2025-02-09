/* Copyright The GNU Toolchain Authors. */

/* libc is required to allocate trampolines.  */
#ifndef inhibit_libc

#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if __APPLE__ && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101400
/* For pthread_jit_write_protect_np */
#include <pthread.h>
#endif

/* HEAP_T_ATTR is provided to allow targets to build the exported functions
   as weak definitions.  */
#ifndef HEAP_T_ATTR
#  define HEAP_T_ATTR
#endif

void *allocate_trampoline_page (void);
int get_trampolines_per_page (void);
struct tramp_ctrl_data *allocate_tramp_ctrl (struct tramp_ctrl_data *parent);
void *allocate_trampoline_page (void);

void __gcc_nested_func_ptr_created (void *chain, void *func, void **dst);
void __gcc_nested_func_ptr_deleted (void);

static const uint8_t trampoline_insns[] = {
  /* movabs $<chain>,%r11  */
  0x49, 0xbb,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

  /* movabs $<func>,%r10  */
  0x49, 0xba,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

  /* rex.WB jmpq *%r11  */
  0x41, 0xff, 0xe3
};

union ix86_trampoline {
  uint8_t insns[sizeof(trampoline_insns)];

  struct __attribute__((packed)) fields {
    uint8_t insn_0[2];
    void *func_ptr;
    uint8_t insn_1[2];
    void *chain_ptr;
    uint8_t insn_2[3];
  } fields;
};

struct tramp_ctrl_data
{
  struct tramp_ctrl_data *prev;

  int free_trampolines;

  /* This will be pointing to an executable mmap'ed page.  */
  union ix86_trampoline *trampolines;
};

int
get_trampolines_per_page (void)
{
  return getpagesize() / sizeof(union ix86_trampoline);
}

static _Thread_local struct tramp_ctrl_data *tramp_ctrl_curr = NULL;

void *
allocate_trampoline_page (void)
{
  void *page;

#if defined(__gnu_linux__)
  page = mmap (0, getpagesize (), PROT_WRITE | PROT_EXEC,
	       MAP_ANON | MAP_PRIVATE, 0, 0);
#elif __APPLE__
# if  __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101400
  page = mmap (0, getpagesize (), PROT_WRITE | PROT_EXEC,
	       MAP_ANON | MAP_PRIVATE | MAP_JIT, 0, 0);
# else
  page = mmap (0, getpagesize (), PROT_WRITE | PROT_EXEC,
	       MAP_ANON | MAP_PRIVATE, 0, 0);
# endif
#else
  page = MAP_FAILED;
#endif

  return page;
}

struct tramp_ctrl_data *
allocate_tramp_ctrl (struct tramp_ctrl_data *parent)
{
  struct tramp_ctrl_data *p = malloc (sizeof (struct tramp_ctrl_data));
  if (p == NULL)
    return NULL;

  p->trampolines = allocate_trampoline_page ();

  if (p->trampolines == MAP_FAILED)
    return NULL;

  p->prev = parent;
  p->free_trampolines = get_trampolines_per_page();

  return p;
}

HEAP_T_ATTR
void
__gcc_nested_func_ptr_created (void *chain, void *func, void **dst)
{
  if (tramp_ctrl_curr == NULL)
    {
      tramp_ctrl_curr = allocate_tramp_ctrl (NULL);
      if (tramp_ctrl_curr == NULL)
	abort ();
    }

  if (tramp_ctrl_curr->free_trampolines == 0)
    {
      void *tramp_ctrl = allocate_tramp_ctrl (tramp_ctrl_curr);
      if (!tramp_ctrl)
	abort ();

      tramp_ctrl_curr = tramp_ctrl;
    }

  union ix86_trampoline *trampoline
    = &tramp_ctrl_curr->trampolines[get_trampolines_per_page ()
				    - tramp_ctrl_curr->free_trampolines];

#if __APPLE__ && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101400
  /* Disable write protection for the MAP_JIT regions in this thread (see
     https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon) */
  pthread_jit_write_protect_np (0);
#endif

  memcpy (trampoline->insns, trampoline_insns,
	  sizeof(trampoline_insns));
  trampoline->fields.func_ptr = func;
  trampoline->fields.chain_ptr = chain;

#if __APPLE__ && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101400
  /* Re-enable write protection.  */
  pthread_jit_write_protect_np (1);
#endif

  tramp_ctrl_curr->free_trampolines -= 1;

  __builtin___clear_cache ((void *)trampoline->insns,
			   ((void *)trampoline->insns + sizeof(trampoline->insns)));

  *dst = &trampoline->insns;
}

HEAP_T_ATTR
void
__gcc_nested_func_ptr_deleted (void)
{
  if (tramp_ctrl_curr == NULL)
    abort ();

  tramp_ctrl_curr->free_trampolines += 1;

  if (tramp_ctrl_curr->free_trampolines == get_trampolines_per_page ())
    {
      if (tramp_ctrl_curr->prev == NULL)
	return;

      munmap (tramp_ctrl_curr->trampolines, getpagesize());
      struct tramp_ctrl_data *prev = tramp_ctrl_curr->prev;
      free (tramp_ctrl_curr);
      tramp_ctrl_curr = prev;
    }
}

#endif /* !inhibit_libc */
