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

#if defined(_WIN32) && defined(_WIN64)
#include <wincrypt.h>
#endif

/* One question, this long after the launch: on device a poll once a second
 * coincided with the game's audio breaking up (docs/STEAM_INPUT.md), so the
 * probe asks once to tell the first call from the polling. */
#define SH_STEAM_INPUT_DELAY_MS 15000

#ifdef _WIN64
typedef bool (__thiscall *get_config_fn)(void *, uint32_t, const char *, uint64_t *, void *);
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
    if (text && memchr(text, 0, size)) {
        HeapFree(GetProcessHeap(), 0, text); text = NULL;
    }
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
    s->gameid = appid;
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
    s->load_result_supported = layout->revision == 202601;
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

/* Separate atomic feed/command files follow the Friends transport, but carry
 * a per-launch nonce and an observed controller-connection lease. Names and
 * selection keys never enter the numeric report. Version 1 is read-only. */
static const wchar_t input_feed[] = L"C:\\madeira-steam-input.bin";
static const wchar_t input_temp[] = L"C:\\madeira-steam-input.tmp";
static const wchar_t input_claim[] = L"C:\\madeira-steam-input-processing.bin";
static const wchar_t input_command[] = L"C:\\madeira-steam-input-cmd.bin";

static void publish_service(struct sh_steam_input *s)
{
    uint8_t data[SH_INPUT_FEED_CAP];
    size_t size = sh_input_service_feed(&s->service, data, sizeof data);
    if (!size) return;
    HANDLE file = CreateFileW(input_temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    bool ok = WriteFile(file, data, (DWORD)size, &wrote, NULL) && wrote == size;
    CloseHandle(file);
    if (ok) ok = MoveFileExW(input_temp, input_feed, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(input_temp);
    else s->service_dirty = false;
}

static void start_service(struct sh_steam_input *s)
{
    wchar_t wide[34]; char hex[33];
    DWORD n = GetEnvironmentVariableW(L"MADEIRA_DOCK_INPUT_SESSION", wide, 34);
    if (n != 32) return;
    for (unsigned i = 0; i < 32; ++i) {
        if (wide[i] > 127) return;
        hex[i] = (char)wide[i];
    }
    hex[32] = 0;
    if (!sh_input_session_parse(hex, s->service.session)) return;
    s->service.enabled = true;
    s->service.available = s->controller != NULL;
    s->service.appid = (uint32_t)s->gameid;
    s->service.state = SH_INPUT_DISCONNECTED;
#if defined(SH_STEAM_INPUT_LOCAL_APPLY) && SH_STEAM_INPUT_LOCAL_APPLY
    s->service.local_apply = s->load_result_supported;
    s->service.working_snapshot = s->service.local_apply;
#endif
    DeleteFileW(L"C:\\madeira-steam-input-baseline.bin"); DeleteFileW(L"C:\\madeira-steam-input-baseline.tmp");
    DeleteFileW(L"C:\\madeira-steam-input-tx.bin"); DeleteFileW(L"C:\\madeira-steam-input-tx.tmp");
    DeleteFileW(L"C:\\madeira-steam-input-apply.bin"); DeleteFileW(L"C:\\madeira-steam-input-apply-processing.bin");
    s->service_dirty = true;
    DeleteFileW(input_feed); DeleteFileW(input_temp); DeleteFileW(input_command); DeleteFileW(input_claim);
    publish_service(s);
}

static bool start_revision_probe(struct sh_steam_input *s);
static uint32_t start_local_apply(struct sh_steam_input *s, const struct sh_input_command *command);
static void poll_local_apply(struct sh_steam_input *s, uint64_t now);


static void poll_service(struct sh_steam_input *s, uint64_t now)
{
    if (!s->service.enabled || now < s->next_service_ms) return;
    s->next_service_ms = now + 500;
    HANDLE file = INVALID_HANDLE_VALUE;
    if (MoveFileExW(input_command, input_claim, MOVEFILE_REPLACE_EXISTING))
        file = CreateFileW(input_claim, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        uint8_t data[SH_INPUT_COMMAND_SIZE + 1]; DWORD got = 0;
        bool read = ReadFile(file, data, sizeof data, &got, NULL);
        /* Claim before reading, so consuming this command cannot delete a
         * newly atomically installed command. */
        CloseHandle(file);
        DeleteFileW(input_claim);
        struct sh_input_command command;
        if (read && sh_input_command_parse(data, got, &command)) {
            uint32_t result = sh_input_service_accept(&s->service, &command);
            if (result != SH_INPUT_STALE) {
                if (result == SH_INPUT_ACK_UNAVAILABLE && command.action == SH_INPUT_APPLY)
                    s->service.result = start_local_apply(s, &command);
                if (result == SH_INPUT_ACK_UNAVAILABLE && command.action == SH_INPUT_REVISION_PROBE)
                    s->service.result = start_revision_probe(s) ? SH_INPUT_OK : SH_INPUT_ACK_UNAVAILABLE;
                if (result == SH_INPUT_OK) {
                    char app[16]; snprintf(app, sizeof app, "%u", (uint32_t)s->gameid);
                    s->selected_workshop = 0;
                    bool selected = ((get_config_fn)CONTROLLER(s, 124))(s->controller, 0, app,
                                                       &s->selected_workshop, &s->selected_url);
                    const char *key = selected ? s->selected_url.text : NULL;
                    size_t length = 0;
                    if (key) while (length <= SH_INPUT_KEY_CAP && key[length]) ++length;
                    if (length > SH_INPUT_KEY_CAP) s->service.result = SH_INPUT_BAD_COMMAND;
                    else {
                        if (length) memcpy(s->service.selection, key, length);
                        s->service.selection[length] = 0;
                        s->service.workshop = selected ? s->selected_workshop : 0;
                    }
                }
                s->service_dirty = true;
            }
        }
    }
    if (s->service_dirty) publish_service(s);
}

void sh_steam_input_start(struct sh_steam_input *s)
{
    s->next_poll_ms = s->o->now_ms() + SH_STEAM_INPUT_DELAY_MS;
    start_service(s);
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
    if (s->layout_queued && !s->activate_at_ms) {
        s->activate_at_ms = s->o->now_ms() + 250;
        s->baseline_dirty = true;
        DeleteFileW(L"C:\\madeira-steam-input-baseline.bin");
    }
}

typedef bool (__thiscall *is_connected_fn)(void *, uint32_t, bool);
typedef bool (__thiscall *has_mapping_fn)(void *, uint32_t);

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
static void queue_layout_bytes(struct sh_steam_input *s, const char *vdf)
{
    size_t length = strlen(vdf);
    char *copy = HeapAlloc(GetProcessHeap(), 0, length + 1);
    if (copy) memcpy(copy, vdf, length + 1);
    DeleteFileW(L"C:\\madeira-steam-input-baseline.bin");
    s->baseline_dirty = true;
    char *previous = s->current_layout;
    s->current_layout = copy;
    s->layout_revision_known = sh_input_vdf_revision(vdf, strlen(vdf), &s->layout_revision);
    s->o->event("steam-input-layout-revision-known", s->layout_revision_known ? 1 : 0);
    if (s->layout_revision_known) s->o->event("steam-input-layout-revision", (int32_t)s->layout_revision);
    struct sh_config_info info = {1, 0, -1, 0x140, -1, 0, 1};
    ((load_vdf_fn)CONTROLLER(s, 38))(s->controller, 0, (uint32_t)s->gameid, vdf, NULL, 0, &info);
    if (previous) HeapFree(GetProcessHeap(), 0, previous);
    s->cache_waits = 0;
    s->layout_queued = true;
    s->service.mapping_cached = false;
    s->service.state = SH_INPUT_QUEUED; s->service_dirty = true;
    s->activate_at_ms = s->o->now_ms() + 3000;
    s->o->event("steam-input-layout-queued", 1); /* void API; not proof of adoption */
}

#if defined(SH_STEAM_INPUT_LOCAL_APPLY) && SH_STEAM_INPUT_LOCAL_APPLY
#ifndef SH_INPUT_VERIFY_HASH
static bool verify_layout_hash(const char *data, size_t size, const uint8_t expected[32])
{
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
    uint8_t actual[32]; DWORD length = sizeof actual;
    bool ok = CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) &&
        CryptHashData(hash, (const BYTE *)data, (DWORD)size, 0) &&
        CryptGetHashParam(hash, HP_HASHVAL, actual, &length, 0) && length == sizeof actual &&
        !memcmp(actual, expected, sizeof actual);
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    return ok;
}
#define SH_INPUT_VERIFY_HASH verify_layout_hash
#endif
static char *copy_layout_span(const char *data, size_t size)
{
    char *copy = HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (copy) { memcpy(copy, data, size); copy[size] = 0; }
    return copy;
}
#endif
static uint32_t start_local_apply(struct sh_steam_input *s, const struct sh_input_command *command)
{
#if !defined(SH_STEAM_INPUT_LOCAL_APPLY) || !SH_STEAM_INPUT_LOCAL_APPLY
    (void)s; (void)command;
    return SH_INPUT_ACK_UNAVAILABLE;
#else
    if (!s->service.local_apply || !s->load_result_supported) return SH_INPUT_ACK_UNAVAILABLE;
    if (s->transaction.phase == SH_INPUT_TX_PREPARED || s->transaction.phase == SH_INPUT_TX_APPLYING ||
        s->transaction.phase == SH_INPUT_TX_ROLLING_BACK || s->probe_original) return SH_INPUT_BUSY;
    if (!s->pad_connected || !s->service.connected) return SH_INPUT_NO_CONTROLLER;
    bool working_observed = s->transaction.working_known ?
        s->transaction.working.revision == s->layout_revision :
        s->load_result.completed && s->load_result.revision == s->layout_revision;
    if (!s->current_layout || !s->layout_queued || s->activate_at_ms || !s->layout_revision_known || !working_observed ||
        s->transaction.phase == SH_INPUT_TX_FAILED || s->transaction.phase == SH_INPUT_TX_CANCELLED) return SH_INPUT_UNAVAILABLE;
    const wchar_t *inbox = L"C:\\madeira-steam-input-apply.bin";
    const wchar_t *claim = L"C:\\madeira-steam-input-apply-processing.bin";
    if (!MoveFileExW(inbox, claim, MOVEFILE_REPLACE_EXISTING)) return SH_INPUT_BAD_COMMAND;
    HANDLE file = CreateFileW(claim, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    uint8_t *data = NULL; DWORD size = 0, got = 0;
    if (file != INVALID_HANDLE_VALUE) {
        size = GetFileSize(file, NULL);
        if (size >= SH_INPUT_APPLY_HEADER && size <= SH_INPUT_APPLY_LIMIT) {
            data = HeapAlloc(GetProcessHeap(), 0, size);
            if (data && (!ReadFile(file, data, size, &got, NULL) || got != size)) {
                HeapFree(GetProcessHeap(), 0, data); data = NULL;
            }
        }
        CloseHandle(file);
    }
    DeleteFileW(claim);
    struct sh_input_apply payload;
    uint32_t result = SH_INPUT_BAD_COMMAND;
    char *apply = NULL, *rollback = NULL, *expected = NULL;
    if (!data || !sh_input_apply_parse(data, size, &payload) || !sh_input_apply_scope(&payload, command) ||
        payload.apply_revision <= s->layout_revision ||
        !SH_INPUT_VERIFY_HASH(payload.apply, payload.apply_size, payload.apply_hash) ||
        !SH_INPUT_VERIFY_HASH(payload.rollback, payload.rollback_size, payload.rollback_hash)) goto done;
    size_t length = strlen(s->current_layout), written = 0;
    expected = HeapAlloc(GetProcessHeap(), 0, length + 48);
    if (!expected || !sh_input_vdf_materialize(s->current_layout, length, payload.rollback_revision,
            expected, length + 48, &written) || written != payload.rollback_size ||
        memcmp(expected, payload.rollback, written)) goto done;
    apply = copy_layout_span(payload.apply, payload.apply_size);
    rollback = copy_layout_span(payload.rollback, payload.rollback_size);
    if (!apply || !rollback) goto done;
    struct sh_input_transaction next = s->transaction;
    if (!next.appid && !sh_input_tx_init(&next, s->service.appid, s->service.epoch, s->layout_revision)) goto done;
    if (!sh_input_tx_reserve(&next, s->service.epoch, command->sequence, payload.apply_revision - 1) ||
        next.apply_revision != payload.apply_revision || next.rollback_revision != payload.rollback_revision ||
        !sh_input_tx_bind(&next, payload.apply_hash, payload.rollback_hash) ||
        !sh_input_tx_queue(&next, s->o->now_ms())) goto done;
    if (s->transaction_apply) HeapFree(GetProcessHeap(), 0, s->transaction_apply);
    if (s->transaction_rollback) HeapFree(GetProcessHeap(), 0, s->transaction_rollback);
    s->transaction_apply = apply; apply = NULL;
    s->transaction_rollback = rollback; rollback = NULL;
    s->transaction = next; s->transaction_dirty = true;
    queue_layout_bytes(s, s->transaction_apply);
    result = SH_INPUT_OK; /* accepted, not acknowledged */
done:
    if (expected) HeapFree(GetProcessHeap(), 0, expected);
    if (apply) HeapFree(GetProcessHeap(), 0, apply);
    if (rollback) HeapFree(GetProcessHeap(), 0, rollback);
    if (data) HeapFree(GetProcessHeap(), 0, data);
    return result;
#endif
}
static void publish_baseline(struct sh_steam_input *s)
{
    if (!s->service.working_snapshot || !s->baseline_dirty || !s->current_layout || !s->pad_connected ||
        !s->layout_queued || s->activate_at_ms || !s->layout_revision_known) return;
    bool settled = s->transaction.sequence ? s->transaction.working_known &&
        s->transaction.working.revision == s->layout_revision :
        s->load_result.completed && s->load_result.revision == s->layout_revision &&
        s->service.state == SH_INPUT_ACTIVATION_REQUESTED;
    if (!settled) return;
    size_t length = strlen(s->current_layout), cap = SH_INPUT_BASELINE_HEADER + length;
    uint8_t *data = HeapAlloc(GetProcessHeap(), 0, cap);
    if (!data) return;
    size_t size = sh_input_baseline_feed(&s->service, s->layout_revision, s->current_layout, length, data, cap);
    HANDLE file = size ? CreateFileW(L"C:\\madeira-steam-input-baseline.tmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL) : INVALID_HANDLE_VALUE;
    bool ok = false;
    if (file != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0; ok = WriteFile(file, data, (DWORD)size, &wrote, NULL) && wrote == size;
        CloseHandle(file);
        if (ok) ok = MoveFileExW(L"C:\\madeira-steam-input-baseline.tmp", L"C:\\madeira-steam-input-baseline.bin",
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    HeapFree(GetProcessHeap(), 0, data);
    if (ok) s->baseline_dirty = false;
    else DeleteFileW(L"C:\\madeira-steam-input-baseline.tmp");
}
static void poll_local_apply(struct sh_steam_input *s, uint64_t now)
{
    if (now >= s->next_baseline_ms) { s->next_baseline_ms = now + 500; publish_baseline(s); }
    if (!s->transaction.appid) return;
    enum sh_input_tx_phase before = s->transaction.phase;
    if (sh_input_tx_poll(&s->transaction, now)) {
        queue_layout_bytes(s, s->transaction_rollback);
    }
    if (s->transaction.phase != before) {
        s->transaction_dirty = true;
        if (s->transaction.phase == SH_INPUT_TX_FAILED) {
            s->activate_at_ms = 0;
            s->service.state = SH_INPUT_FAILED; s->service_dirty = true;
        }
    }
    if (!s->transaction_dirty || now < s->next_transaction_ms) return;
    s->next_transaction_ms = now + 500;
    uint8_t bytes[SH_INPUT_TX_FEED_SIZE];
    size_t size = sh_input_tx_feed(&s->service, &s->transaction, bytes, sizeof bytes);
    if (!size) return;
    HANDLE file = CreateFileW(L"C:\\madeira-steam-input-tx.tmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    bool ok = WriteFile(file, bytes, (DWORD)size, &wrote, NULL) && wrote == size;
    CloseHandle(file);
    if (ok) ok = MoveFileExW(L"C:\\madeira-steam-input-tx.tmp", L"C:\\madeira-steam-input-tx.bin",
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (ok) s->transaction_dirty = false;
    else DeleteFileW(L"C:\\madeira-steam-input-tx.tmp");
}

#if defined(SH_STEAM_INPUT_REVISION_PROBE) && SH_STEAM_INPUT_REVISION_PROBE
static char *revision_queue_copy(const char *source, uint32_t revision)
{
    size_t size = strlen(source), written = 0;
    char *copy = HeapAlloc(GetProcessHeap(), 0, size + 48);
    if (copy && !sh_input_vdf_materialize(source, size, revision, copy, size + 48, &written)) {
        HeapFree(GetProcessHeap(), 0, copy); return NULL;
    }
    return copy;
}
#endif

/* A diagnostic build only, never a normal apply/restore implementation.
 * One fixed synthetic fixture, once per launch; no paths in the command,
 * no persisted Steam selection changes and exact baseline bytes retained. */
static bool start_revision_probe(struct sh_steam_input *s)
{
#if !defined(SH_STEAM_INPUT_REVISION_PROBE) || !SH_STEAM_INPUT_REVISION_PROBE
    (void)s;
    return false;
#else
    if (!s->load_result_supported || !s->service.enabled || !s->pad_connected ||
        !s->layout_queued || s->activate_at_ms || s->probe_used || s->probe_original || s->transaction.sequence ||
        s->selected_workshop || !s->layout_revision_known || s->layout_revision > INT32_MAX - 2) return false;
    char *original = read_layout(s, s->selected_url.text);
    uint32_t revision = 0;
    if (!original) return false;
    if (!sh_input_vdf_revision(original, strlen(original), &revision) || revision != s->layout_revision) {
        HeapFree(GetProcessHeap(), 0, original); return false;
    }
    char *fixture = read_layout(s, "madeira_m1_revision_probe.vdf");
    uint32_t fixture_revision = 0;
    if (!fixture || !sh_input_vdf_revision(fixture, strlen(fixture), &fixture_revision) || fixture_revision != 731) {
        if (fixture) HeapFree(GetProcessHeap(), 0, fixture);
        HeapFree(GetProcessHeap(), 0, original); return false;
    }
    /* Reserve different revisions for apply AND restore. In particular, a
     * delayed startup completion must never acknowledge a later rollback.
     * One-shot gating prevents these numbers being reused in this launch. */
    uint32_t first = revision > fixture_revision ? revision : fixture_revision;
    if (first > INT32_MAX - 2) {
        HeapFree(GetProcessHeap(), 0, fixture); HeapFree(GetProcessHeap(), 0, original); return false;
    }
    char *apply = revision_queue_copy(fixture, first + 1);
    char *restore = revision_queue_copy(original, first + 2);
    HeapFree(GetProcessHeap(), 0, fixture);
    if (!apply || !restore) {
        if (apply) HeapFree(GetProcessHeap(), 0, apply);
        if (restore) HeapFree(GetProcessHeap(), 0, restore);
        HeapFree(GetProcessHeap(), 0, original); return false;
    }
    s->probe_original = original; s->probe_restore = restore;
    s->probe_revision = first + 1; s->probe_original_revision = first + 2;
    s->probe_used = true; s->probe_phase = 1;
    s->probe_deadline_ms = s->o->now_ms() + 15000;
    s->o->event("steam-input-probe-phase", 1); /* applying; not acknowledged */
    queue_layout_bytes(s, apply);
    HeapFree(GetProcessHeap(), 0, apply);
    return true;
#endif
}

static void poll_revision_probe(struct sh_steam_input *s, uint64_t now)
{
    if (!s->probe_original || !s->probe_deadline_ms || now < s->probe_deadline_ms) return;
    if (s->probe_phase == 1 || s->probe_phase == 2) {
        s->probe_phase = 3;
        s->probe_deadline_ms = now + 15000;
        s->o->event("steam-input-probe-phase", 3); /* restoring */
        queue_layout_bytes(s, s->probe_restore);
    } else if (s->probe_phase == 3) {
        s->probe_phase = 5; s->probe_deadline_ms = 0;
        s->o->event("steam-input-probe-phase", 5); /* restore observation timed out */
        /* Keep the exact baseline until session cleanup. No active claim. */
    }
}

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
        s->service.state = SH_INPUT_FAILED; s->service_dirty = true;
        ++s->layout_attempts;
        return;
    }
    if (!selected || !s->selected_url.text || !*s->selected_url.text) {
        struct sh_utl_string app_key = {app, {0, 0}}, url = {gamepad_template, {0, 0}};
        ((select_fn)CONTROLLER(s, 114))(s->controller, 0, &app_key, 0, &url, false, true);
        s->service.selected_fallback = true; s->service_dirty = true;
        s->o->event("steam-input-selected", 1);
    }
    queue_layout_bytes(s, vdf);
    HeapFree(GetProcessHeap(), 0, vdf);
}

void sh_steam_input_poll(struct sh_steam_input *s)
{
    if (!s->next_poll_ms) return;
    uint64_t now = s->o->now_ms();
    poll_service(s, now);
    if (!s->controller) return;
    poll_revision_probe(s, now);
    poll_local_apply(s, now);
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
        if (s->service.connected != connected) {
            sh_input_service_connection(&s->service, connected);
            if (s->transaction.appid) {
                sh_input_tx_disconnect(&s->transaction, s->service.epoch);
                s->transaction_dirty = true;
            }
            DeleteFileW(L"C:\\madeira-steam-input-baseline.bin"); s->baseline_dirty = true;
            s->service_dirty = true;
        }
        if (!connected) {
            s->pad_connected = false;
            s->layout_queued = false;
            s->layout_attempts = 0;
            s->activate_at_ms = 0;
        } else {
            if (!s->pad_connected) s->o->event("steam-input-connected", 1);
            s->pad_connected = true;
            if (!s->probe_original && !s->transaction.sequence && !s->layout_queued && s->layout_attempts < 3) load_layout(s);
        }
    }
    if (s->activate_at_ms && now >= s->activate_at_ms) {
        if ((s->transaction.phase == SH_INPUT_TX_APPLYING || s->transaction.phase == SH_INPUT_TX_ROLLING_BACK) &&
            !s->transaction.completed) { s->activate_at_ms = now + 100; return; }
        s->activate_at_ms = 0;
        bool cached = ((has_mapping_fn)CONTROLLER(s, 128))(s->controller, (uint32_t)s->gameid);
        s->o->event("steam-input-mapping-cached", cached ? 1 : 0);
        s->service.mapping_cached = cached; s->service_dirty = true;
        if (cached) s->service.state = SH_INPUT_CACHED;
        if (!cached) {
            if (++s->cache_waits < 5) s->activate_at_ms = now + 1000;
            else { s->layout_queued = false; ++s->layout_attempts;
                s->service.state = SH_INPUT_FAILED; s->service_dirty = true; }
        } else {
            /* Recompute the enable decision after the async cache load, not
             * just before it. Never activate the previous empty mapping. */
            ((reset_cache_fn)CONTROLLER(s, 131))(s->controller);
            ((warm_fn)CONTROLLER(s, 41))(s->controller, 0, (uint32_t)s->gameid);
            ((activate_fn)CONTROLLER(s, 40))(s->controller, 0, (uint32_t)s->gameid, false);
            s->service.state = SH_INPUT_ACTIVATION_REQUESTED; s->service_dirty = true;
            s->o->event("steam-input-activation-requested", 1);
            if (s->transaction.appid && sh_input_tx_activated(&s->transaction, s->layout_revision, now)) {
                s->transaction_dirty = true;
                if (s->transaction.working_known) s->service.state = SH_INPUT_ACTIVE;
            }
        }
    }
    if (s->last_count < 0 && now >= s->next_poll_ms) {
        int32_t count = ((connected_fn)CONTROLLER(s, 22))(s->controller);
        s->last_count = count < 0 ? 0 : count;
        s->o->event("steam-input-controllers", count);
    }
}

void sh_steam_input_callback(struct sh_steam_input *s, int32_t id, const void *data, int32_t size)
{
    if (s->controller && s->next_poll_ms && s->load_result_supported && id == 1060003 && size >= 0) {
        struct sh_input_load_result result;
        if (!sh_input_load_result_parse(data, (size_t)size, &result) ||
            result.controller != 0 || result.appid != s->gameid) return;
        s->load_result = result;
        if (s->transaction.appid && sh_input_tx_completion(&s->transaction, &result, s->o->now_ms()))
            s->transaction_dirty = true;
        if (result.completed && s->probe_original) {
            if (s->probe_phase == 1 && result.revision == s->probe_revision) {
                s->probe_phase = 2;
                s->probe_deadline_ms = s->o->now_ms() + 30000;
                s->o->event("steam-input-probe-phase", 2); /* observed; mapping still needs device proof */
            } else if (s->probe_phase == 3 && result.revision == s->probe_original_revision) {
                s->probe_phase = 4; s->probe_deadline_ms = 0;
                HeapFree(GetProcessHeap(), 0, s->probe_original); s->probe_original = NULL;
                HeapFree(GetProcessHeap(), 0, s->probe_restore); s->probe_restore = NULL;
                s->o->event("steam-input-probe-phase", 4); /* original revision observed */
            }
        }
        if (s->load_result_events < 16) {
            s->o->event("steam-input-load-result-events", (int32_t)++s->load_result_events);
            s->o->event("steam-input-load-result-completed", result.completed ? 1 : 0);
            s->o->event("steam-input-load-result-revision", (int32_t)result.revision);
            s->o->event("steam-input-load-result-matches-revision", result.completed &&
                        s->layout_revision_known && result.revision == s->layout_revision ? 1 : 0);
        }
        /* Observation only until a distinctive revision/mapping proves its
         * ordering relative to engine activation. Never advances service state. */
        return;
    }
    struct sh_input_configuration configuration;
    if (!s->controller || !s->next_poll_ms || id != 2803 || size < 0 ||
        !sh_input_configuration_parse(data, (size_t)size, &configuration) ||
        configuration.appid != s->gameid) return;
    s->configuration = configuration;
    /* Bound report growth. Device/creator identities never enter the report.
     * In particular, these manifest revisions do not mark a VDF revision active. */
    if (s->configuration_events < 16) {
        s->o->event("steam-input-config-loaded-events", (int32_t)++s->configuration_events);
        s->o->event("steam-input-config-loaded-major", (int32_t)configuration.major);
        s->o->event("steam-input-config-loaded-minor", (int32_t)configuration.minor);
        s->o->event("steam-input-config-loaded-flags", (configuration.native_actions ? 1 : 0) |
                    (configuration.gamepad_output ? 2 : 0));
    }
}

void sh_steam_input_end(struct sh_steam_input *s)
{
    s->next_poll_ms = 0; s->activate_at_ms = 0;
    memset(&s->transaction, 0, sizeof s->transaction);
    s->transaction_dirty = false; s->next_transaction_ms = 0;
    if (s->current_layout) { HeapFree(GetProcessHeap(), 0, s->current_layout); s->current_layout = NULL; }
    if (s->transaction_apply) { HeapFree(GetProcessHeap(), 0, s->transaction_apply); s->transaction_apply = NULL; }
    if (s->transaction_rollback) { HeapFree(GetProcessHeap(), 0, s->transaction_rollback); s->transaction_rollback = NULL; }
    DeleteFileW(L"C:\\madeira-steam-input-baseline.bin"); DeleteFileW(L"C:\\madeira-steam-input-baseline.tmp");
    DeleteFileW(L"C:\\madeira-steam-input-tx.bin"); DeleteFileW(L"C:\\madeira-steam-input-tx.tmp");
    DeleteFileW(L"C:\\madeira-steam-input-apply.bin"); DeleteFileW(L"C:\\madeira-steam-input-apply-processing.bin");
    if (s->probe_original) { HeapFree(GetProcessHeap(), 0, s->probe_original); s->probe_original = NULL; }
    if (s->probe_restore) { HeapFree(GetProcessHeap(), 0, s->probe_restore); s->probe_restore = NULL; }
    if (s->service.enabled) {
        DeleteFileW(input_feed); DeleteFileW(input_temp); DeleteFileW(input_command); DeleteFileW(input_claim);
        s->service.enabled = false;
    }
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
void sh_steam_input_callback(struct sh_steam_input *s, int32_t id, const void *data, int32_t size)
{ (void)s; (void)id; (void)data; (void)size; }
void sh_steam_input_end(struct sh_steam_input *s) { (void)s; }
#endif
