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
typedef void (__thiscall *warm_fn)(void *, uint32_t, uint32_t);
/* Valve's CUtlString: the callee reads only its first field, the text. */
struct sh_utl_string { const char *text; uint64_t unused[2]; };
typedef void (__thiscall *select_fn)(void *, uint32_t, const struct sh_utl_string *, uint64_t,
                                     const struct sh_utl_string *, bool, bool);
/* Steam's own PS5 "Gamepad" template (controller_base/templates). */
static const char gamepad_template[] = "controller_ps5_gamepad_joystick.vdf";
/* The 28-byte block the cache copies with each layout (CSteamController
 * slot 106 refuses NULL). SteamUI.dll passes these defaults, the same the
 * client gives a new cache entry; zeros made the layout unusable. */
struct sh_config_info {
    int32_t one;
    uint8_t flag04;
    int32_t minus08;
    uint16_t value0c;
    int32_t minus10;
    uint8_t flag14;
    int32_t kind18; /* 1; 4 for Steam's own desktop layout (app 769) */
};
_Static_assert(sizeof(struct sh_config_info) == 28, "SteamUI's info block is 28 bytes");
typedef void (__thiscall *load_vdf_fn)(void *, uint32_t, uint32_t, const char *, const void *, uint64_t,
                                       const struct sh_config_info *);

/* The template's text, read from Steam's folder (MADEIRA_STEAM_HOST_CLIENT_DIR),
 * or NULL. The caller frees it with HeapFree. */
static bool basename_is_safe(const char *name)
{
    return name && *name && !strstr(name, "..") && !strpbrk(name, "\\/:");
}

/* GetConfigForAppAndController returns a selection key, not a URL. SteamUI's
 * resolver (January SteamUI.dll 0x544440) adds template:// to a bare name and
 * resolves AC-app-device to the account's local autosave. Accept the old
 * template:// spelling too, because earlier Dock builds saved that spelling. */
static char *read_layout(const struct sh_steam_input *s, const char *selection)
{
    wchar_t root[MAX_PATH], path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"MADEIRA_STEAM_HOST_CLIENT_DIR", root, MAX_PATH);
    if (!n || n >= MAX_PATH) return NULL;
    while (n > 3 && (root[n - 1] == L'\\' || root[n - 1] == L'/')) root[--n] = 0;
    const char *name = selection;
    if (!name || !*name) name = gamepad_template;
    if (!strncmp(name, "template://", 11)) name += 11;
    if (!strncmp(name, "AC-", 3)) {
        char app[16];
        snprintf(app, sizeof app, "%u", (uint32_t)s->gameid);
        size_t app_len = strlen(app);
        if (strncmp(name + 3, app, app_len) || name[3 + app_len] != '-') return NULL;
        const char *device = name + 4 + app_len;
        if (!basename_is_safe(device) || strchr(device, '-')) return NULL;
        wchar_t device_name[MAX_PATH], relative[MAX_PATH];
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, device, -1, device_name, MAX_PATH)) return NULL;
        int len = swprintf(relative, MAX_PATH,
                           L"\\steamapps\\common\\Steam Controller Configs\\%u\\config\\%u\\%ls.vdf",
                           s->account_id, (uint32_t)s->gameid, device_name);
        if (len < 0 || n + (size_t)len >= MAX_PATH) return NULL;
        wcscpy(path, root); wcscat(path, relative);
    } else if (basename_is_safe(name)) {
        /* A Workshop ID is returned separately. Other selection keys need
         * their own resolver and must never be mistaken for a template. */
        size_t len = strlen(name);
        if (len < 5 || strcmp(name + len - 4, ".vdf")) return NULL;
        wchar_t basename[MAX_PATH];
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, basename, MAX_PATH)) return NULL;
        const wchar_t folder[] = L"\\controller_base\\templates\\";
        if (n + wcslen(folder) + wcslen(basename) >= MAX_PATH) return NULL;
        wcscpy(path, root); wcscat(path, folder); wcscat(path, basename);
    } else {
        /* Also accept a resolved file beneath Steam's directory. */
        wchar_t relative[MAX_PATH];
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, relative, MAX_PATH)) return NULL;
        for (wchar_t *p = relative; *p; ++p) if (*p == L'/') *p = L'\\';
        if (!wcsncmp(relative, L"controller_base\\", 16)) {
            if (n + 1 + wcslen(relative) >= MAX_PATH) return NULL;
            wcscpy(path, root); wcscat(path, L"\\"); wcscat(path, relative);
        } else wcscpy(path, relative);
        if (_wcsnicmp(path, root, n) || path[n] != L'\\' || wcsstr(path + n, L"..") ||
            wcschr(path + n, L':')) return NULL;
    }
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
typedef void (__thiscall *set_focused_fn)(void *, const uint64_t *, bool, bool, uint32_t, uint16_t, uint16_t);
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
                            void *client_user, uint64_t steamid, uint32_t appid, const struct dock_client_layout *layout,
                            const struct sh_observer *o)
{
    memset(s, 0, sizeof *s);
    s->o = o;
    s->last_count = -1;
    s->output_reserved = s->output_connected = -1;
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
        !dock_method_is(base, controller, 85, layout->controller_connected) ||
        !dock_method_is(base, controller, 124, layout->controller_get_config) ||
        !dock_method_is(base, controller, 128, layout->controller_has_mapping) ||
        !dock_method_is(base, controller, 41, layout->controller_warm)) {
        o->event("steam-input-available", 0);
        return;
    }
    s->controller = controller;
    o->event("steam-input-available", 1);
    s->gameid = appid; /* CGameID of a Steam app: the app ID, type 0 */
    s->account_id = (uint32_t)steamid; /* local autosave directory; never reported */
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
    ++s->sdl_attempts;
    sdl_init_fn init = (sdl_init_fn)(void *)GetProcAddress(sdl, "SDL_InitSubSystem");
    sdl_joysticks_fn joysticks = (sdl_joysticks_fn)(void *)GetProcAddress(sdl, "SDL_GetJoysticks");
    sdl_free_fn release = (sdl_free_fn)(void *)GetProcAddress(sdl, "SDL_free");
    if (!init || !joysticks || !release) {
        s->o->event("steam-input-sdl-init", -1);
        return;
    }
    s->sdl_started = init(SH_SDL_INIT_GAMEPAD);
    s->o->event("steam-input-sdl-init", s->sdl_started ? 1 : 0);
    if (!s->sdl_started) return;
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
    ((set_focused_fn)(*(void ***)s->utils)[48])(s->utils, &s->gameid, true, false, pid, 0, 0);
    s->o->event("steam-input-focus-reported", 1);
    uint64_t focused = 0;
    ((get_focused_fn)(*(void ***)s->utils)[101])(s->utils, &focused);
    s->o->event("steam-input-focused-app", (int32_t)(focused & 0xffffff));
    if (s->layout_queued && !s->activate_at_ms) s->activate_at_ms = s->o->now_ms() + 250;
}

typedef bool (__thiscall *is_connected_fn)(void *, uint32_t, bool);
typedef bool (__thiscall *has_mapping_fn)(void *, uint32_t);
typedef bool (__thiscall *get_config_fn)(void *, uint32_t, const char *, uint64_t *, void *);

/* Observe only; never create or write Steam's stream. January's
 * CXInputSteamMemStream (0xa48340) maps 0x3e78 bytes with masks at 0/4
 * (reserved/connected) and XINPUT_STATE[16] at 0x934. The packet counter is
 * excluded from the change check. This separates a loaded binding from
 * actual output and reports no button/axis values. */
static void observe_output(struct sh_steam_input *s)
{
    if (!s->output_view) {
        s->output_map = OpenFileMappingW(FILE_MAP_READ, false, L"SteamXInput_mem-IPCWrapper");
        if (!s->output_map) return;
        s->output_view = MapViewOfFile(s->output_map, FILE_MAP_READ, 0, 0, 0x3e78);
        if (!s->output_view) { CloseHandle(s->output_map); s->output_map = NULL; return; }
        s->o->event("steam-input-output-stream", 1);
    }
    uint32_t reserved, connected;
    memcpy(&reserved, s->output_view, sizeof reserved);
    memcpy(&connected, s->output_view + 4, sizeof connected);
    if ((int32_t)reserved != s->output_reserved) {
        s->output_reserved = (int32_t)reserved;
        s->o->event("steam-input-output-reserved", (int32_t)reserved);
    }
    if ((int32_t)connected != s->output_connected) {
        s->output_connected = (int32_t)connected;
        s->o->event("steam-input-output-connected", (int32_t)connected);
    }
    if ((connected & 1) && !s->output_changed) {
        uint8_t state[12];
        memcpy(state, s->output_view + 0x938, sizeof state);
        if (memcmp(s->output_state, state, sizeof state)) {
            s->output_changed = true;
            s->o->event("steam-input-output-changed", 1);
        }
    }
}

/* Layout setup is independent of focus changes. SDL may find the pad after
 * the game window already exists, and a pad can reconnect with the same PID.
 * Query the cheap indexed status, not GetNumConnectedControllers (on device
 * that enumeration interrupted audio). Requeue only on connection or failure. */
static void load_layout(struct sh_steam_input *s)
{
    char app[16];
    snprintf(app, sizeof app, "%u", (uint32_t)s->gameid);
    s->selected_workshop = 0;
    bool selected = ((get_config_fn)CONTROLLER(s, 124))(s->controller, 0, app,
                                                       &s->selected_workshop, &s->selected_url);
    /* Refresh the client's per-account selection before loading, as SteamUI
     * does. It also refreshes the controller metadata used by the loader. */
    const char *url = selected ? s->selected_url.text : NULL;
    s->o->event("steam-input-layout-source", !url || !*url ? 0 :
               !strncmp(url, "template://", 11) ? 1 :
               strlen(url) > 2 && url[1] == ':' ? 2 :
               !strncmp(url, "controller_base", 15) ? 3 :
               !strncmp(url, "AC-", 3) ? 6 : basename_is_safe(url) ? 5 : 4);
    char *vdf = s->selected_workshop ? NULL : read_layout(s, url);
    if (!vdf) {
        s->o->event("steam-input-layout-unavailable", 1);
        ++s->layout_attempts;
        return;
    }
    if (!selected || !s->selected_url.text || !*s->selected_url.text) {
        struct sh_utl_string app_key = {app, {0, 0}}, url = {gamepad_template, {0, 0}};
        ((select_fn)CONTROLLER(s, 114))(s->controller, 0, &app_key, 0, &url, false, true);
        s->o->event("steam-input-selected", 1);
    }
    struct sh_config_info info = {1, 0, -1, 0x140, -1, 0, 1};
    ((load_vdf_fn)CONTROLLER(s, 38))(s->controller, 0, (uint32_t)s->gameid, vdf, NULL, 0, &info);
    HeapFree(GetProcessHeap(), 0, vdf);
    s->cache_waits = 0;
    s->layout_queued = true;
    s->activate_at_ms = s->o->now_ms() + 3000;
    s->o->event("steam-input-layout-queued", 1); /* void API; not proof of adoption */
}

void sh_steam_input_poll(struct sh_steam_input *s)
{
    if (!s->controller || !s->next_poll_ms) return;
    uint64_t now = s->o->now_ms();
    if (now >= s->next_output_ms) {
        s->next_output_ms = now + 100;
        observe_output(s);
    }
    if (!s->sdl_started && s->sdl_attempts < 3 && now >= s->next_sdl_ms) {
        s->next_sdl_ms = now + 1000;
        start_sdl_gamepads(s);
    }
    if (s->utils && now >= s->next_focus_ms) {
        s->next_focus_ms = now + 1000;
        report_focus(s);
    }
    if (s->sdl_started && now >= s->next_controller_ms) {
        s->next_controller_ms = now + 5000;
        bool connected = ((is_connected_fn)CONTROLLER(s, 85))(s->controller, 0, false);
        if (!connected) {
            s->pad_connected = false;
            s->layout_queued = false;
            s->layout_attempts = 0;
            s->activate_at_ms = 0;
        } else {
            if (!s->pad_connected) s->o->event("steam-input-connected", 1);
            s->pad_connected = true;
            if (!s->layout_queued && s->layout_attempts < 3) load_layout(s);
        }
    }
    if (s->activate_at_ms && now >= s->activate_at_ms) {
        s->activate_at_ms = 0;
        bool cached = ((has_mapping_fn)CONTROLLER(s, 128))(s->controller, (uint32_t)s->gameid);
        s->o->event("steam-input-mapping-cached", cached ? 1 : 0);
        if (!cached) {
            if (++s->cache_waits < 5) s->activate_at_ms = now + 1000;
            else { s->layout_queued = false; ++s->layout_attempts; }
        } else {
            /* Recompute the enable decision after the async cache load, not
             * just before it. Never activate the previous empty mapping. */
            ((reset_cache_fn)CONTROLLER(s, 131))(s->controller);
            ((warm_fn)CONTROLLER(s, 41))(s->controller, 0, (uint32_t)s->gameid);
            ((activate_fn)CONTROLLER(s, 40))(s->controller, 0, (uint32_t)s->gameid, false);
            s->o->event("steam-input-activation-requested", 1);
        }
    }
    if (s->last_count < 0 && now >= s->next_poll_ms) {
        int32_t count = ((connected_fn)CONTROLLER(s, 22))(s->controller);
        s->last_count = count < 0 ? 0 : count;
        s->o->event("steam-input-controllers", count);
    }
}

void sh_steam_input_end(struct sh_steam_input *s)
{
    if (s->output_view) UnmapViewOfFile(s->output_view);
    if (s->output_map) CloseHandle(s->output_map);
    s->output_view = NULL; s->output_map = NULL;
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
                            void *client_user, uint64_t steamid, uint32_t appid, const struct dock_client_layout *layout,
                            const struct sh_observer *o)
{
    (void)module; (void)engine; (void)pipe; (void)client_user; (void)steamid; (void)appid; (void)layout;
    memset(s, 0, sizeof *s);
    s->o = o;
}
void sh_steam_input_start(struct sh_steam_input *s) { (void)s; }
void sh_steam_input_poll(struct sh_steam_input *s) { (void)s; }
void sh_steam_input_end(struct sh_steam_input *s) { (void)s; }
#endif
