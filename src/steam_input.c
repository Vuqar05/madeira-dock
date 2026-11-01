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
typedef void *(__thiscall *get_utils_fn)(void *, int32_t);
typedef void (__thiscall *activate_fn)(void *, uint32_t, uint32_t, bool);
/* Valve's CUtlString: the callee reads only its first field, the text. */
struct sh_utl_string { const char *text; uint64_t unused[2]; };
typedef void (__thiscall *select_fn)(void *, uint32_t, const struct sh_utl_string *, uint64_t,
                                     const struct sh_utl_string *, bool, bool);
/* Steam's own PS5 "Gamepad" template (controller_base/templates). */
static const char gamepad_template[] = "template://controller_ps5_gamepad_joystick.vdf";
static const wchar_t gamepad_template_file[] = L"\\controller_base\\templates\\controller_ps5_gamepad_joystick.vdf";
/* The 28-byte block the cache copies with each layout (CSteamController
 * slot 106 refuses NULL); desktop Steam's values are not known, zero here. */
struct sh_config_info { uint8_t bytes[28]; };
typedef uint32_t (__thiscall *binding_fn)(void *, const char *);
typedef bool (__thiscall *load_vdf_fn)(void *, uint32_t, uint32_t, const char *, const void *, uint64_t,
                                       const struct sh_config_info *);

/* The template's text, read from Steam's folder (MADEIRA_STEAM_HOST_CLIENT_DIR),
 * or NULL. The caller frees it with HeapFree. */
static char *read_template(void)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"MADEIRA_STEAM_HOST_CLIENT_DIR", path, MAX_PATH);
    if (!n || n + wcslen(gamepad_template_file) >= MAX_PATH) return NULL;
    wcscat(path, gamepad_template_file);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(file, NULL), got = 0;
    char *text = size && size < (1u << 20) ? HeapAlloc(GetProcessHeap(), 0, size + 1) : NULL;
    if (text && (!ReadFile(file, text, size, &got, NULL) || got != size)) {
        HeapFree(GetProcessHeap(), 0, text);
        text = NULL;
    }
    CloseHandle(file);
    if (text) text[size] = 0;
    return text;
}
typedef bool (__thiscall *set_focused_fn)(void *, const uint64_t *, bool, bool, uint32_t, uint16_t, uint16_t);
typedef uint64_t *(__thiscall *get_focused_fn)(void *, uint64_t *);

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
        !dock_method_is(base, controller, 131, layout->controller_reset_cache) ||
        !dock_method_is(base, controller, 40, layout->controller_activate) ||
        !dock_method_is(base, controller, 114, layout->controller_select) ||
        !dock_method_is(base, controller, 38, layout->controller_load_vdf) ||
        !dock_method_is(base, controller, 43, layout->controller_binding)) {
        o->event("steam-input-available", 0);
        return;
    }
    s->controller = controller;
    o->event("steam-input-available", 1);
    s->gameid = appid; /* CGameID of a Steam app: the app ID, type 0 */
    if (dock_method_is(base, engine, 14, layout->engine_utils)) {
        void *utils = ((get_utils_fn)(*(void ***)engine)[14])(engine, pipe);
        if (utils && dock_method_is(base, utils, 48, layout->utils_set_focused) &&
            dock_method_is(base, utils, 101, layout->utils_get_focused)) s->utils = utils;
    }
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

/* The client builds its PlayStation (and other non-Steam) controllers from
 * SDL's joysticks (CHIDDeviceListSDL, sdl://N devices). Desktop Steam starts
 * SDL's joystick subsystem; in a headless client nothing does, so SDL never
 * looks for pads (docs/STEAM_INPUT.md). Once the client has loaded SDL3.dll,
 * Dock starts it with SDL's public SDL_InitSubSystem, the same call. Only a
 * module lookup until then: no client call, nothing polled. */
#define SH_SDL_INIT_GAMEPAD 0x00002000u
typedef bool (__cdecl *sdl_init_fn)(uint32_t);
typedef uint32_t *(__cdecl *sdl_joysticks_fn)(int *);
typedef void (__cdecl *sdl_free_fn)(void *);

static void start_sdl_gamepads(struct sh_steam_input *s)
{
    HMODULE sdl = GetModuleHandleW(L"SDL3.dll");
    if (!sdl) return;
    s->sdl_started = true;
    sdl_init_fn init = (sdl_init_fn)(void *)GetProcAddress(sdl, "SDL_InitSubSystem");
    sdl_joysticks_fn joysticks = (sdl_joysticks_fn)(void *)GetProcAddress(sdl, "SDL_GetJoysticks");
    sdl_free_fn release = (sdl_free_fn)(void *)GetProcAddress(sdl, "SDL_free");
    if (!init || !joysticks || !release) {
        s->o->event("steam-input-sdl-init", -1);
        return;
    }
    s->o->event("steam-input-sdl-init", init(SH_SDL_INIT_GAMEPAD) ? 1 : 0);
    int count = 0;
    uint32_t *ids = joysticks(&count);
    if (ids) release(ids);
    s->o->event("steam-input-sdl-joysticks", count);
}

/* Steam Input activates a game's layout ("Queueing activation for
 * controller") for the game that has focus, which desktop Steam's UI reports
 * through IClientUtils::SetFocusedWindow. A headless client has no UI, so Dock
 * reports the foreground window's process once it is another process, and
 * again when it changes: a window lookup each second, a client call only on
 * a change. Arguments: the game, focused, not the overlay, its process. */
static void report_focus(struct sh_steam_input *s)
{
    DWORD pid = 0;
    HWND window = GetForegroundWindow();
    if (window) GetWindowThreadProcessId(window, &pid);
    if (!pid || pid == GetCurrentProcessId() || pid == s->focus_pid) return;
    s->focus_pid = pid;
    s->o->event("steam-input-focus-pid", (int32_t)pid);
    bool done = ((set_focused_fn)(*(void ***)s->utils)[48])(s->utils, &s->gameid, true, false, pid, 0, 0);
    s->o->event("steam-input-focus-set", done ? 1 : 0);
    uint64_t focused = 0;
    ((get_focused_fn)(*(void ***)s->utils)[101])(s->utils, &focused);
    s->o->event("steam-input-focused-app", (int32_t)(focused & 0xffffff));
    /* Focus alone queued no activation on device: ask the controller manager
     * to activate the game's layout for the first controller, as its own
     * activation path does (CSteamController, "Queueing activation"). */
    /* No layout is selected for the game in a headless client: desktop
     * Steam's UI selects one (SetActiveConfigForApp in its controller
     * settings). Select Steam's PS5 Gamepad template, as the UI does. */
    char app[16];
    snprintf(app, sizeof app, "%u", (uint32_t)s->gameid);
    struct sh_utl_string app_key = {app, {0, 0}}, url = {gamepad_template, {0, 0}};
    ((select_fn)CONTROLLER(s, 114))(s->controller, 0, &app_key, 0, &url, false, true);
    s->o->event("steam-input-selected", 1);
    /* Selecting is not enough: desktop Steam's UI reads the layout and hands
     * its text to the client ("Warming Config Cache", then the client's "Add
     * to Config Cache Request ... adopting binding"); the activation uses that
     * cache. Without the UI nothing fills it, so Dock does. */
    char *vdf = read_template();
    if (vdf) {
        struct sh_config_info info = {{0}};
        /* The cache adopts a binding made from the same text ("adopting
         * binding N"); with handle 0 it adopted none. */
        uint32_t binding = ((binding_fn)CONTROLLER(s, 43))(s->controller, vdf);
        s->o->event("steam-input-binding", (int32_t)binding);
        bool loaded = ((load_vdf_fn)CONTROLLER(s, 38))(s->controller, 0, (uint32_t)s->gameid, vdf, NULL, binding, &info);
        HeapFree(GetProcessHeap(), 0, vdf);
        s->o->event("steam-input-layout-loaded", loaded ? 1 : 0);
    } else s->o->event("steam-input-layout-loaded", -1);
    ((activate_fn)CONTROLLER(s, 40))(s->controller, 0, (uint32_t)s->gameid, false);
    s->o->event("steam-input-activated", 1);
}

void sh_steam_input_poll(struct sh_steam_input *s)
{
    if (s->utils && s->next_poll_ms && s->o->now_ms() >= s->next_focus_ms) {
        s->next_focus_ms = s->o->now_ms() + 1000;
        report_focus(s);
    }
    if (s->controller && !s->sdl_started && s->o->now_ms() >= s->next_sdl_ms) {
        s->next_sdl_ms = s->o->now_ms() + 500;
        start_sdl_gamepads(s);
    }
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
