/*
 *	$Id$
 */

#include "ef_tblfunc_loader.h"

#ifndef NO_DYNAMIC_LOAD_TABLE

#include <stdio.h> /* NULL */

/* --- global functions --- */

void* ef_load_8bits_func(const char* symname) {
  static bl_dl_handle_t handle;
  static int is_tried;

  if (!is_tried) {
    is_tried = 1;

    if (!(handle = bl_dl_open(MEFLIB_DIR, "ef_8bits")) &&
        !(handle = bl_dl_open("", "ef_8bits"))) {
      return NULL;
    }

    bl_dl_close_at_exit(handle);
  }

  if (handle) {
    return bl_dl_func_symbol(handle, symname);
  } else {
    return NULL;
  }
}

void* ef_load_jajp_func(const char* symname) {
  static bl_dl_handle_t handle;
  static int is_tried;

  if (!is_tried) {
    is_tried = 1;

    if (!(handle = bl_dl_open(MEFLIB_DIR, "ef_jajp")) && !(handle = bl_dl_open("", "ef_jajp"))) {
      return NULL;
    }

    bl_dl_close_at_exit(handle);
  }

  if (handle) {
    return bl_dl_func_symbol(handle, symname);
  } else {
    return NULL;
  }
}

void* ef_load_kokr_func(const char* symname) {
  static bl_dl_handle_t handle;
  static int is_tried;

  if (!is_tried) {
    is_tried = 1;

    if (!(handle = bl_dl_open(MEFLIB_DIR, "ef_kokr")) && !(handle = bl_dl_open("", "ef_kokr"))) {
      return NULL;
    }

    bl_dl_close_at_exit(handle);
  }

  if (handle) {
    return bl_dl_func_symbol(handle, symname);
  } else {
    return NULL;
  }
}

void* ef_load_zh_func(const char* symname) {
  static bl_dl_handle_t handle;
  static int is_tried;

  if (!is_tried) {
    is_tried = 1;

    if (!(handle = bl_dl_open(MEFLIB_DIR, "ef_zh")) && !(handle = bl_dl_open("", "ef_zh"))) {
      return NULL;
    }

    bl_dl_close_at_exit(handle);
  }

  if (handle) {
    return bl_dl_func_symbol(handle, symname);
  } else {
    return NULL;
  }
}

#endif /* NO_DYNAMIC_LOAD_TABLE */