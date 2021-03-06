/* -*- c-basic-offset:2; tab-width:2; indent-tabs-mode:nil -*- */

#include "bl_cycle_index.h"

#include "bl_mem.h"
#include "bl_debug.h"

/* --- global functions --- */

bl_cycle_index_t *bl_cycle_index_new(u_int size) {
  bl_cycle_index_t *cycle;

  if (size == 0) {
#ifdef DEBUG
    bl_warn_printf(BL_DEBUG_TAG " the size of cycle index should be greater than 0.\n");
#endif

    return NULL;
  }

  if ((cycle = malloc(sizeof(bl_cycle_index_t))) == NULL) {
#ifdef DEBUG
    bl_warn_printf(BL_DEBUG_TAG " malloc() failed.\n");
#endif

    return NULL;
  }

  cycle->size = size;
  cycle->start = 0;
  cycle->next = 0;
  cycle->is_init = 1;

  return cycle;
}

void bl_cycle_index_destroy(bl_cycle_index_t *cycle) {
  free(cycle);
}

void bl_cycle_index_reset(bl_cycle_index_t *cycle) {
  cycle->start = 0;
  cycle->next = 0;
  cycle->is_init = 1;
}

void bl_cycle_index_rollback(bl_cycle_index_t *cycle, u_int num) {
  if (bl_get_filled_cycle_index(cycle) <= num) {
    cycle->is_init = 1;
    cycle->next = cycle->start;
  } else {
    if (cycle->next < num) {
      cycle->next += cycle->size;
    }
    cycle->next -= num;
  }
}

/*
 * !! Notice !!
 * this resets the "start" member 0.
 */
void bl_cycle_index_change_size(bl_cycle_index_t *cycle, u_int new_size) {
  u_int filled;

  if ((filled = bl_get_filled_cycle_index(cycle)) == 0) {
    cycle->size = new_size;
    bl_cycle_index_reset(cycle);

    return;
  }

  cycle->size = new_size;
  cycle->start = 0;

  if (filled >= new_size) {
    cycle->next = 0;
  } else {
    cycle->next = filled;
  }
}

u_int bl_get_cycle_index_size(bl_cycle_index_t *cycle) { return cycle->size; }

u_int bl_get_filled_cycle_index(bl_cycle_index_t *cycle) {
  if (cycle->is_init) {
    return 0;
  } else if (cycle->next == cycle->start) {
    return cycle->size;
  } else if (cycle->next > cycle->start) {
    return cycle->next - cycle->start;
  } else {
    return cycle->next + cycle->size - cycle->start;
  }
}

int bl_cycle_index_of(bl_cycle_index_t *cycle, int at) {
  if (cycle->start + at >= cycle->size) {
    if (cycle->start + at - cycle->size >= cycle->size) {
#ifdef DEBUG
      bl_warn_printf(BL_DEBUG_TAG " index(%d) is strange.\n", at);
#endif

      return -1;
    } else {
      return cycle->start + at - cycle->size;
    }
  } else {
    return cycle->start + at;
  }
}

int bl_next_cycle_index(bl_cycle_index_t *cycle) {
  int next;

  if (cycle->is_init) {
    cycle->is_init = 0;
  } else if (cycle->next == cycle->start) {
    if (++cycle->start == cycle->size) {
      cycle->start = 0;
    }
  }

  next = cycle->next;

  if (++cycle->next == cycle->size) {
    cycle->next = 0;
  }

  return next;
}
