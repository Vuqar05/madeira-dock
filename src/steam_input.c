/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md
 * Steam Input through Valve's client: the client's own controller manager
 * reads the pad and applies the user's Steam Input layout. This first step
 * only asks it how many controllers it sees. Slots and ABI:
 * docs/CLIENT_LAYOUTS.md, "Steam Input research".
 */
#include "steam_input.h"
#include <string.h>

/* One question, this long after the launch: on device a poll once a second
 * coincided with the game's audio breaking up (docs/STEAM_INPUT.md), so the
 * probe asks once to tell the first call from the polling. */
#define SH_STEAM_INPUT_DELAY_MS 15000

#ifdef _WIN64
typedef void *(__thiscall *get_controller_fn)(void *, int32_t);
typedef int32_t (__thiscall *connected_fn)(void *);

/* Off unless the app asks for it (MADEIRA_DOCK_STEAM_INPUT=1). */
static bool enabled(void)
{
    wchar_t value[4] = {0};
    return GetEnvironmentVariableW(L"MADEIRA_DOCK_STEAM_INPUT", value, 4) == 1 && value[0] == L'1';
}

void sh_steam_input_init(struct sh_steam_input *s, HMODULE module, void *engine, int32_t pipe,
                         const struct dock_client_layout *layout, const struct sh_observer *o)
{
    memset(s, 0, sizeof *s);
    s->o = o;
    s->last_count = -1;
    if (!enabled()) return;
    uintptr_t base = (uintptr_t)module;
    if (!layout || !dock_method_is(base, engine, 66, layout->engine_controller)) {
        o->event("steam-input-available", 0);
        return;
    }
    void *controller = ((get_controller_fn)(*(void ***)engine)[66])(engine, pipe);
    if (!controller || !dock_method_is(base, controller, 22, layout->controller_count)) {
        o->event("steam-input-available", 0);
        return;
    }
    s->controller = controller;
    s->next_poll_ms = o->now_ms() + SH_STEAM_INPUT_DELAY_MS;
    o->event("steam-input-available", 1);
}

void sh_steam_input_poll(struct sh_steam_input *s)
{
    if (!s->controller || s->last_count >= 0 || s->o->now_ms() < s->next_poll_ms) return;
    int32_t count = ((connected_fn)(*(void ***)s->controller)[22])(s->controller);
    s->last_count = count < 0 ? 0 : count;
    s->o->event("steam-input-controllers", count);
}
#else
void sh_steam_input_init(struct sh_steam_input *s, HMODULE module, void *engine, int32_t pipe,
                         const struct dock_client_layout *layout, const struct sh_observer *o)
{
    (void)module; (void)engine; (void)pipe; (void)layout;
    memset(s, 0, sizeof *s);
    s->o = o;
}
void sh_steam_input_poll(struct sh_steam_input *s) { (void)s; }
#endif
