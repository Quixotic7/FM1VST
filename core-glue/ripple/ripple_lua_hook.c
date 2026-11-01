/* SPDX-License-Identifier: GPL-3.0-only */
/* Ripple's runaway droplets (docs/VIRTUAL-CARTS.md section 2): on the FM-1 the watchdog ended a droplet that never
 * gave the main loop back; a host can't interrupt wasm3 or an AudioWorklet mid-call, so Lua bounds itself. Every new
 * Lua state gets a count hook (luaport.h's overlay: luai_userstateopen -> rp_lua_open; coroutines inherit it): each
 * main-loop pass (core_ripple.c fm1core_pl_loop: rp_lua_budget) may run RP_LUA_BUDGET instructions of Lua, in all its
 * callbacks (key, _tick, frame, init ..); past that the hook raises "droplet took too long" in the droplet, which
 * stops it (api.c lr_pcall: the error screen) as any other error does. The hook keeps raising until the next pass, so
 * a droplet's own pcall can't swallow it. */
#define LUA_FM1 1
#include <stdint.h>
#include "lua.h"
#include "lauxlib.h"

#define RP_LUA_EVERY 10000                /* instructions between two calls of the hook */
#define RP_LUA_BUDGET 500u                /* calls a pass: 5,000,000 instructions (~0.1-0.5 s interpreted) */

static uint32_t rp_lua_left = RP_LUA_BUDGET;
static uint32_t rp_lua_stops;             /* passes the hook stopped (since power-on) */

static void rp_lua_hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    if (rp_lua_left && !--rp_lua_left)
        rp_lua_stops++;
    if (!rp_lua_left) {
        luaL_error(L, "droplet took too long (over %d instructions without giving the FM-1 back)",
                   (int)(RP_LUA_EVERY * RP_LUA_BUDGET));
    }
}

void rp_lua_open(void *L);
void rp_lua_open(void *L)                 /* a new state (lstate.c lua_newstate) */
{
    lua_sethook((lua_State *)L, rp_lua_hook, LUA_MASKCOUNT, RP_LUA_EVERY);
    rp_lua_left = RP_LUA_BUDGET;
}

void rp_lua_budget(void);
void rp_lua_budget(void) { rp_lua_left = RP_LUA_BUDGET; }
uint32_t rp_lua_stopped(void);
uint32_t rp_lua_stopped(void) { return rp_lua_stops; }
