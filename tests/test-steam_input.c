/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md
 * Exercise the production lifecycle without Steam, Wine, accounts or devices. */
#define MADEIRA_STEAM_HOST_SESSION_H
#define _WIN64
#define __thiscall
#define __cdecl
#include "probe.h"
#include "client_layout.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>
#include <wctype.h>
#define CP_UTF8 65001
#define MB_ERR_INVALID_CHARS 8
static int MultiByteToWideChar(unsigned cp, unsigned flags, const char *text, int size, wchar_t *out, int cap)
{ (void)cp; (void)flags; assert(size == -1); size_t n = mbstowcs(out, text, (size_t)cap); return n < (size_t)cap ? (int)n + 1 : 0; }
static int _wcsnicmp(const wchar_t *a, const wchar_t *b, size_t n)
{ while (n--) { if (towlower(*a) != towlower(*b)) return 1; if (!*a++) return 0; ++b; } return 0; }
typedef void *HMODULE, *HWND, *HANDLE;
typedef uint32_t DWORD;
#define MAX_PATH 260
#define GENERIC_READ 1
#define FILE_SHARE_READ 1
#define OPEN_EXISTING 1
#define FILE_MAP_READ 4
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
static uint64_t clock_ms;
static bool pad, sdl_ok, template_ok = true, personal, cached;
static unsigned loads, activates, selections, resets, warms, counts, focus_calls, sdl_calls;
static DWORD foreground_pid = 77;
static char order[64];
static unsigned order_count;
static bool output_available, output_map_ok;
static uint8_t output_memory[0x3e78];
static unsigned output_opens, output_closes, output_unmaps, output_events, output_changes;
static wchar_t opened_path[MAX_PATH];
static uint64_t now_ms(void) { return clock_ms; }
static void event(const char *name, int32_t value)
{
    if (!strncmp(name, "steam-input-output-", 19)) ++output_events;
    if (!strcmp(name, "steam-input-output-changed")) { assert(value == 1); ++output_changes; }
}
static DWORD GetEnvironmentVariableW(const wchar_t *name, wchar_t *out, DWORD cap)
{
    const wchar_t *value = !wcscmp(name, L"MADEIRA_DOCK_STEAM_INPUT") ? L"1" : L"C:\\Steam";
    assert(wcslen(value) < cap); wcscpy(out, value); return (DWORD)wcslen(value);
}
static HMODULE GetModuleHandleW(const wchar_t *name) { (void)name; return (void *)1; }
static bool sdl_init(uint32_t flags) { assert(flags == 0x2000); ++sdl_calls; return sdl_ok; }
static uint32_t *sdl_joysticks(int *count) { *count = pad; return NULL; }
static void *GetProcAddress(HMODULE module, const char *name)
{
    (void)module;
    if (!strcmp(name, "SDL_InitSubSystem")) return (void *)sdl_init;
    if (!strcmp(name, "SDL_GetJoysticks")) return (void *)sdl_joysticks;
    if (!strcmp(name, "SDL_free")) return (void *)free;
    return NULL;
}
static HANDLE GetProcessHeap(void) { return NULL; }
static void *HeapAlloc(HANDLE heap, unsigned flags, size_t size) { (void)heap; (void)flags; return malloc(size); }
static void HeapFree(HANDLE heap, unsigned flags, void *ptr) { (void)heap; (void)flags; free(ptr); }
static HANDLE CreateFileW(const wchar_t *path, unsigned a, unsigned b, void *c, unsigned d, unsigned e, void *f)
{
    wcscpy(opened_path, path); (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
    return template_ok ? (HANDLE)1 : INVALID_HANDLE_VALUE;
}
static DWORD GetFileSize(HANDLE file, void *high) { (void)file; (void)high; return 2; }
static bool ReadFile(HANDLE file, void *out, DWORD size, DWORD *got, void *overlapped)
{ (void)file; (void)overlapped; assert(size == 2); memcpy(out, "{}", 2); *got = 2; return true; }
static void CloseHandle(HANDLE file) { if (file == (HANDLE)2) ++output_closes; }
static HANDLE OpenFileMappingW(unsigned access, bool inherit, const wchar_t *name)
{
    assert(access == FILE_MAP_READ && !inherit && !wcscmp(name, L"SteamXInput_mem-IPCWrapper"));
    ++output_opens; return output_available ? (HANDLE)2 : NULL;
}
static const void *MapViewOfFile(HANDLE file, unsigned access, unsigned high, unsigned low, size_t size)
{
    assert(file == (HANDLE)2 && access == FILE_MAP_READ && !high && !low && size == sizeof output_memory);
    return output_map_ok ? output_memory : NULL;
}
static void UnmapViewOfFile(const void *view) { assert(view == output_memory); ++output_unmaps; }
static HWND GetForegroundWindow(void) { return (HWND)1; }
static void GetWindowThreadProcessId(HWND window, DWORD *pid) { (void)window; *pid = foreground_pid; }
static DWORD GetCurrentProcessId(void) { return 11; }
#include "../src/steam_input.c"

static bool is_connected(void *this, uint32_t index, bool mode)
{ (void)this; assert(index == 0 && !mode); return pad; }
static int32_t count(void *this) { (void)this; ++counts; return pad; }
static bool get_config(void *this, uint32_t index, const char *app, uint64_t *workshop, void *url)
{
    (void)this; assert(index == 0 && !strcmp(app, "480")); *workshop = personal ? 123 : 0;
    *(const char **)url = personal ? "" : "controller_ps5_gamepad_joystick.vdf";
    return true;
}
static void select_config(void *this, uint32_t index, const struct sh_utl_string *app,
                          uint64_t workshop, const struct sh_utl_string *url, bool a, bool b)
{ (void)this; (void)index; (void)app; (void)workshop; (void)url; (void)a; (void)b; ++selections; }
static void load(void *this, uint32_t index, uint32_t app, const char *vdf, const void *optional,
                 uint64_t id, const struct sh_config_info *info)
{
    (void)this; assert(index == 0 && app == 480 && !strcmp(vdf, "{}") && !optional && !id);
    assert(info->one == 1 && info->minus08 == -1 && info->value0c == 0x140 && info->kind18 == 1);
    ++loads;
}
static bool has_mapping(void *this, uint32_t app) { (void)this; assert(app == 480); return cached; }
static void reset_cache(void *this) { (void)this; ++resets; order[order_count++] = 'R'; }
static void warm(void *this, uint32_t index, uint32_t app)
{ (void)this; assert(index == 0 && app == 480); ++warms; order[order_count++] = 'W'; }
static void activate(void *this, uint32_t index, uint32_t app, bool flag)
{ (void)this; assert(index == 0 && app == 480 && !flag); ++activates; order[order_count++] = 'A'; }
static void focused(void *this, const uint64_t *app, bool yes, bool overlay, uint32_t pid, uint16_t a, uint16_t b)
{ (void)this; assert(*app == 480 && yes && !overlay && pid == foreground_pid && !a && !b); ++focus_calls; }
static uint64_t *get_focused(void *this, uint64_t *out) { (void)this; *out = 480; return out; }

int main(void)
{
    void *methods[149] = {0}, *utils_methods[102] = {0};
    void **controller = methods, **utils = utils_methods;
    methods[22] = (void *)count; methods[38] = (void *)load; methods[40] = (void *)activate;
    methods[41] = (void *)warm; methods[85] = (void *)is_connected;
    methods[114] = (void *)select_config; methods[124] = (void *)get_config; methods[131] = (void *)reset_cache; methods[128] = (void *)has_mapping;
    utils_methods[48] = (void *)focused; utils_methods[101] = (void *)get_focused;
    struct sh_observer o = {now_ms, NULL, event};
    struct sh_steam_input s = {0}; s.o = &o; s.controller = &controller; s.utils = &utils; s.gameid = 480; s.last_count = -1;
    s.account_id = 42;
    const char *template_names[] = {NULL, "controller_ps5_gamepad_joystick.vdf",
        "template://controller_ps5_gamepad_joystick.vdf",
        "controller_base/templates/controller_ps5_gamepad_joystick.vdf",
        "C:\\Steam\\controller_base\\templates\\controller_ps5_gamepad_joystick.vdf"};
    for (unsigned i = 0; i < sizeof template_names / sizeof *template_names; ++i) {
        char *vdf = read_layout(&s, template_names[i]); assert(vdf); free(vdf);
        assert(!wcscmp(opened_path, L"C:\\Steam\\controller_base\\templates\\controller_ps5_gamepad_joystick.vdf"));
    }
    char *autosave = read_layout(&s, "AC-480-controller_ps5"); assert(autosave); free(autosave);
    assert(!wcscmp(opened_path, L"C:\\Steam\\steamapps\\common\\Steam Controller Configs\\42\\config\\480\\controller_ps5.vdf"));
    const char *bad_names[] = {"AC-481-controller_ps5", "AC-480-../other", "AC-480-C:other",
        "template://../other.vdf", "../other.vdf", "C:\\Other\\layout.vdf", "workshop://123", "LOCAL_fixture"};
    for (unsigned i = 0; i < sizeof bad_names / sizeof *bad_names; ++i) assert(!read_layout(&s, bad_names[i]));
    sh_steam_input_start(&s);
    sh_steam_input_poll(&s); /* SDL init fails; same game already has focus. */
    assert(sdl_calls == 1 && !s.sdl_started && !loads && focus_calls == 1);
    sdl_ok = true; clock_ms = 1000; sh_steam_input_poll(&s);
    assert(sdl_calls == 2 && s.sdl_started && !loads);
    pad = true; clock_ms = 6000; sh_steam_input_poll(&s);
    assert(loads == 1 && !activates && !selections && focus_calls == 1);
    clock_ms = 8999; sh_steam_input_poll(&s); assert(!activates);
    clock_ms = 9000; sh_steam_input_poll(&s); assert(!activates && s.activate_at_ms == 10000);
    cached = true; clock_ms = 10000; sh_steam_input_poll(&s);
    assert(activates == 1 && resets == 1 && warms == 1 && !memcmp(order, "RWA", 3));
    clock_ms = 15000; sh_steam_input_poll(&s); assert(counts == 1 && loads == 1);
    foreground_pid = 88; clock_ms = 16000; sh_steam_input_poll(&s);
    clock_ms = 16250; sh_steam_input_poll(&s); assert(activates == 2 && loads == 1);
    pad = false; clock_ms = 21000; sh_steam_input_poll(&s); assert(!s.layout_queued);
    pad = true; clock_ms = 26000; sh_steam_input_poll(&s); assert(loads == 2);
    clock_ms = 29000; sh_steam_input_poll(&s); assert(activates == 3);
    /* Failed reads are retryable without a foreground-PID change. */
    s.layout_queued = false; template_ok = false; clock_ms = 31000; sh_steam_input_poll(&s);
    assert(loads == 2 && s.layout_attempts == 1);
    template_ok = true; clock_ms = 36000; sh_steam_input_poll(&s); assert(loads == 3);
    clock_ms = 39000; sh_steam_input_poll(&s); assert(activates == 4);
    /* A user's personal selection is never silently replaced by our template. */
    s.layout_queued = false; personal = true; clock_ms = 41000; sh_steam_input_poll(&s);
    assert(loads == 3 && !selections && s.layout_attempts == 2 && counts == 1);
    clock_ms = 46000; sh_steam_input_poll(&s); assert(s.layout_attempts == 3 && loads == 3);
    s.controller = NULL; s.activate_at_ms = 1; clock_ms = 100000; sh_steam_input_poll(&s);
    assert(activates == 4); /* unavailable adapter is inert, even with stale deadlines */
    /* Diagnostic reads are bounded and read-only, tolerate late mappings and
     * failed views, and exclude packet counters/disconnected state. */
    unsigned previous_opens = output_opens;
    observe_output(&s); assert(output_opens == previous_opens + 1 && !s.output_view);
    output_available = true;
    observe_output(&s); assert(!s.output_view && !s.output_map && output_closes == 1);
    output_map_ok = true; s.output_reserved = s.output_connected = -1;
    observe_output(&s); assert(s.output_view == output_memory && output_events == 3 && !output_changes);
    previous_opens = output_opens;
    output_memory[0] = 1; output_memory[4] = 1;
    observe_output(&s); assert(output_events == 5 && !output_changes && output_opens == previous_opens);
    output_memory[0x934] = 7; /* only the packet counter changed */
    observe_output(&s); assert(!output_changes);
    output_memory[4] = 0; output_memory[0x938] = 1;
    observe_output(&s); assert(!output_changes);
    output_memory[4] = 1;
    uint8_t before[sizeof output_memory]; memcpy(before, output_memory, sizeof before);
    observe_output(&s); assert(output_changes == 1 && s.output_changed);
    observe_output(&s); assert(output_changes == 1 && !memcmp(before, output_memory, sizeof before));
    sh_steam_input_end(&s); assert(!s.output_view && !s.output_map && output_closes == 2 && output_unmaps == 1);
    puts("PASS: Steam selection keys/autosaves, path bounds, late pad, SDL retry, delayed cache activation, refocus, reconnect and personal selection");
    puts("PASS: output diagnostic retries and cleanup, read-only bounded views, packet-only changes and disconnected state");
}
