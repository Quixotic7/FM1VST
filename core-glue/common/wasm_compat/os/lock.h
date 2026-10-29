/* SPDX-License-Identifier: GPL-3.0-only */
/* Apple's os_unfair_lock in an .rfm1 build (fm1_add_rfm1 puts core-glue/common/wasm_compat on the include path of
 * every unit; native builds never see it). ChoralRoot's emulator unit (tools/emu/emu_hal_fw.h) takes it for its core-1
 * thread, which a core never starts (fm1core.h: one thread; common/env.h: getenv is NULL), so the lock is a flag. */
#pragma once
typedef struct { int v; } os_unfair_lock;
#define OS_UNFAIR_LOCK_INIT {0}
static inline void os_unfair_lock_lock(os_unfair_lock *l) { l->v = 1; }
static inline int os_unfair_lock_trylock(os_unfair_lock *l) { if (l->v) return 0; l->v = 1; return 1; }
static inline void os_unfair_lock_unlock(os_unfair_lock *l) { l->v = 0; }
