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
#include <stdio.h>
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

/* The client reads two keys when it builds a game's opt-in mask: the per-game
 * Steam Input override (0 forced off, 1 default, 2 forced on) and the global
 * PlayStation setting. Which IClientUser store number holds them is not known
 * yet, so in stores 1 to 4 both are read and logged, the override goes to
 * "forced on" (the game's mask) and the PlayStation setting to "every game"
 * (the client-wide mask, which decides which pads the client opens); see
 * docs/STEAM_INPUT.md. */
static const char ps_support[] = "SteamController_PSSupport";
#define SH_OVERRIDE_DEFAULT 1
#define SH_OVERRIDE_ON 2
#define SH_PS_DEFAULT 1   /* games without PlayStation support */
#define SH_PS_ALL 2
#define SH_STORES 4

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
                            void *client_user, uint32_t appid, const struct dock_client_layout *layout,
                            const struct sh_observer *o)
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
        o->event("steam-input-config", -1);
        return;
    }
    get_config_int_fn get = (get_config_int_fn)(*(void ***)client_user)[18];
    set_config_int_fn set = (set_config_int_fn)(*(void ***)client_user)[17];
    snprintf(s->override_key, sizeof s->override_key, "%u\\UseSteamControllerConfig", appid);
    static const char *const ps_events[] = {"steam-input-store1-ps", "steam-input-store2-ps",
                                            "steam-input-store3-ps", "steam-input-store4-ps"};
    static const char *const app_events[] = {"steam-input-store1-app", "steam-input-store2-app",
                                             "steam-input-store3-app", "steam-input-store4-app"};
    static const char *const set_events[] = {"steam-input-store1-set", "steam-input-store2-set",
                                             "steam-input-store3-set", "steam-input-store4-set"};
    for (uint32_t store = 1; store <= SH_STORES; ++store) {
        unsigned i = store - 1;
        int32_t value = 0;
        bool found = get(client_user, store, ps_support, &value);
        o->event(ps_events[i], found ? value : -1);
        s->saved_ps[i] = found ? value : SH_PS_DEFAULT;
        value = 0;
        found = get(client_user, store, s->override_key, &value);
        o->event(app_events[i], found ? value : -1);
        s->saved_override[i] = found ? value : SH_OVERRIDE_DEFAULT;
        /* bit 0 the override, bit 1 the PlayStation setting */
        int32_t done = set(client_user, store, s->override_key, SH_OVERRIDE_ON) ? 1 : 0;
        if (set(client_user, store, ps_support, SH_PS_ALL)) done |= 2;
        o->event(set_events[i], done);
        s->changed_override |= (uint32_t)done << (2 * i);
    }
    if (s->changed_override) s->user = client_user;
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
    set_config_int_fn set = (set_config_int_fn)(*(void ***)s->user)[17];
    for (uint32_t store = 1; store <= SH_STORES; ++store) {
        uint32_t done = s->changed_override >> (2 * (store - 1));
        if (done & 1) set(s->user, store, s->override_key, s->saved_override[store - 1]);
        if (done & 2) set(s->user, store, ps_support, s->saved_ps[store - 1]);
    }
    s->o->event("steam-input-override-restored", (int32_t)s->changed_override);
    s->user = NULL;
}
#else
void sh_steam_input_prepare(struct sh_steam_input *s, HMODULE module, void *engine, int32_t pipe,
                            void *client_user, uint32_t appid, const struct dock_client_layout *layout,
                            const struct sh_observer *o)
{
    (void)module; (void)engine; (void)pipe; (void)client_user; (void)appid; (void)layout;
    memset(s, 0, sizeof *s);
    s->o = o;
}
void sh_steam_input_start(struct sh_steam_input *s) { (void)s; }
void sh_steam_input_poll(struct sh_steam_input *s) { (void)s; }
void sh_steam_input_end(struct sh_steam_input *s) { (void)s; }
#endif
