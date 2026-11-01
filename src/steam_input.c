/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md
 * Steam Input through Valve's client: the client's own controller manager
 * reads the pad and applies the user's Steam Input layout. Dock turns on the
 * client's PlayStation support for the launch and asks once how many
 * controllers it sees. Slots and ABI: docs/CLIENT_LAYOUTS.md, "Steam Input
 * research".
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
typedef void (__thiscall *ps_changed_fn)(void *, bool);
typedef void (__thiscall *reset_cache_fn)(void *);
typedef bool (__thiscall *get_config_int_fn)(void *, uint32_t, const char *, int32_t *);
typedef bool (__thiscall *set_config_int_fn)(void *, uint32_t, const char *, int32_t);

/* The user's local config store, where the client reads the PlayStation opt-in
 * (0 off, 1 games without PlayStation support, 2 every game). */
#define SH_CONFIG_STORE_USER_LOCAL 3
static const char ps_support[] = "SteamController_PSSupport";

#define CONTROLLER(s, n) ((*(void ***)(s)->controller)[n])

/* Off unless the app asks for it (MADEIRA_DOCK_STEAM_INPUT=1). */
static bool enabled(void)
{
    wchar_t value[4] = {0};
    return GetEnvironmentVariableW(L"MADEIRA_DOCK_STEAM_INPUT", value, 4) == 1 && value[0] == L'1';
}

/* What the client's settings page does after the PlayStation setting changes:
 * its controller manager rebuilds which pads Steam Input takes. */
static void settings_changed(struct sh_steam_input *s)
{
    ((ps_changed_fn)CONTROLLER(s, 80))(s->controller, true);
    ((reset_cache_fn)CONTROLLER(s, 131))(s->controller);
    s->o->event("steam-input-settings-changed", 1);
}

void sh_steam_input_prepare(struct sh_steam_input *s, HMODULE module, void *engine, int32_t pipe,
                            void *client_user, const struct dock_client_layout *layout,
                            const struct sh_observer *o)
{
    memset(s, 0, sizeof *s);
    s->o = o;
    s->last_count = -1;
    s->saved_ps_support = -1;
    if (!enabled()) return;
    uintptr_t base = (uintptr_t)module;
    if (!layout || !dock_method_is(base, engine, 66, layout->engine_controller)) {
        o->event("steam-input-available", 0);
        return;
    }
    void *controller = ((get_controller_fn)(*(void ***)engine)[66])(engine, pipe);
    if (!controller || !dock_method_is(base, controller, 22, layout->controller_count) ||
        !dock_method_is(base, controller, 80, layout->controller_ps_changed) ||
        !dock_method_is(base, controller, 131, layout->controller_reset_cache)) {
        o->event("steam-input-available", 0);
        return;
    }
    s->controller = controller;
    o->event("steam-input-available", 1);
    if (!dock_method_is(base, client_user, 18, layout->get_config_int) ||
        !dock_method_is(base, client_user, 17, layout->set_config_int)) {
        o->event("steam-input-ps-support", -1);
        return;
    }
    int32_t value = 1;
    if (!((get_config_int_fn)(*(void ***)client_user)[18])(client_user, SH_CONFIG_STORE_USER_LOCAL, ps_support, &value))
        value = 1;   /* unset: the client's own default */
    o->event("steam-input-ps-support", value);
    if (value != 2) {
        bool set = ((set_config_int_fn)(*(void ***)client_user)[17])(client_user, SH_CONFIG_STORE_USER_LOCAL, ps_support, 2);
        o->event("steam-input-ps-support-set", set);
        if (!set) return;
        s->user = client_user;
        s->saved_ps_support = value;
    }
    settings_changed(s);
}

void sh_steam_input_start(struct sh_steam_input *s)
{
    s->next_poll_ms = s->o->now_ms() + SH_STEAM_INPUT_DELAY_MS;
}

void sh_steam_input_poll(struct sh_steam_input *s)
{
    if (!s->controller || s->last_count >= 0 || !s->next_poll_ms || s->o->now_ms() < s->next_poll_ms) return;
    int32_t count = ((connected_fn)CONTROLLER(s, 22))(s->controller);
    s->last_count = count < 0 ? 0 : count;
    s->o->event("steam-input-controllers", count);
}

void sh_steam_input_end(struct sh_steam_input *s)
{
    if (!s->user) return;
    ((set_config_int_fn)(*(void ***)s->user)[17])(s->user, SH_CONFIG_STORE_USER_LOCAL, ps_support, s->saved_ps_support);
    s->o->event("steam-input-ps-support-restored", s->saved_ps_support);
    s->user = NULL;
}
#else
void sh_steam_input_prepare(struct sh_steam_input *s, HMODULE module, void *engine, int32_t pipe,
                            void *client_user, const struct dock_client_layout *layout,
                            const struct sh_observer *o)
{
    (void)module; (void)engine; (void)pipe; (void)client_user; (void)layout;
    memset(s, 0, sizeof *s);
    s->o = o;
}
void sh_steam_input_start(struct sh_steam_input *s) { (void)s; }
void sh_steam_input_poll(struct sh_steam_input *s) { (void)s; }
void sh_steam_input_end(struct sh_steam_input *s) { (void)s; }
#endif
