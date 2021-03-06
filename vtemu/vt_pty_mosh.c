/* -*- c-basic-offset:2; tab-width:2; indent-tabs-mode:nil -*- */

#include "vt_pty_intern.h"

#include <stdio.h> /* NULL */

#ifndef NO_DYNAMIC_LOAD_SSH

#include <pobl/bl_dlfcn.h>
#include <pobl/bl_debug.h>

#ifndef LIBDIR
#define MOSHLIB_DIR "/usr/local/lib/mlterm/"
#else
#define MOSHLIB_DIR LIBDIR "/mlterm/"
#endif

#if 0
#define __DEBUG
#endif

/* --- static variables --- */

static vt_pty_t *(*mosh_new)(const char *, char **, char **, const char *, const char *,
                             const char *, const char *, u_int, u_int, u_int, u_int);
static int (*mosh_set_use_loopback)(vt_pty_t *, int);
static int (*mosh_set_pty_read_trigger)(void (*func)(void));

static int is_tried;
static bl_dl_handle_t handle;

#ifdef USE_WIN32API
static void (*trigger_pty_read)(void);
#endif

/* --- static functions --- */

static void load_library(void) {
  is_tried = 1;

  if (!(handle = bl_dl_open(MOSHLIB_DIR, "ptymosh")) && !(handle = bl_dl_open("", "ptymosh"))) {
    bl_error_printf("MOSH: Could not load.\n");

    return;
  }

  bl_dl_close_at_exit(handle);

  mosh_new = bl_dl_func_symbol(handle, "vt_pty_mosh_new");
  mosh_set_use_loopback = bl_dl_func_symbol(handle, "vt_pty_mosh_set_use_loopback");

#ifdef USE_WIN32API
  mosh_set_pty_read_trigger = bl_dl_func_symbol(handle, "vt_pty_mosh_set_pty_read_trigger");
  if (trigger_pty_read) {
    vt_pty_mosh_set_pty_read_trigger(trigger_pty_read);
  }
#endif
}

#endif

/* --- global functions --- */

vt_pty_t *vt_pty_mosh_new(const char *cmd_path, char **cmd_argv, char **env, const char *uri,
                          const char *pass, const char *pubkey, const char *privkey, u_int cols,
                          u_int rows, u_int width_pix, u_int height_pix) {
#ifndef NO_DYNAMIC_LOAD_SSH
  if (!is_tried) {
    load_library();
  }

  if (mosh_new) {
    return (*mosh_new)(cmd_path, cmd_argv, env, uri, pass, pubkey, privkey, cols, rows, width_pix,
                       height_pix);
  }
#endif

  return NULL;
}

int vt_pty_mosh_set_use_loopback(vt_pty_t *pty, int use) {
#ifndef NO_DYNAMIC_LOAD_SSH
  if (mosh_set_use_loopback) {
    return (*mosh_set_use_loopback)(pty, use);
  }
#endif

  return 0;
}

#ifdef USE_WIN32API
void vt_pty_mosh_set_pty_read_trigger(void (*func)(void)) {
#ifndef NO_DYNAMIC_LOAD_SSH
  /* This function can be called before vt_pty_mosh_new() */
  if (!is_tried) {
    trigger_pty_read = func;
  } else if (mosh_set_pty_read_trigger) {
    (*mosh_set_pty_read_trigger)(func);
  }
#endif
}
#endif
