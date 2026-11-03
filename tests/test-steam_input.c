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
#define GENERIC_WRITE 2
#define CREATE_ALWAYS 2
#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8
#define GENERIC_READ 1
#define FILE_SHARE_READ 1
#define OPEN_EXISTING 1
#define FILE_MAP_READ 4
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
static bool service_enabled, command_ready, command_claimed, service_write_ok = true;
static bool payload_ready, payload_claimed;
#if defined(SH_STEAM_INPUT_LOCAL_APPLY) && SH_STEAM_INPUT_LOCAL_APPLY
static bool hash_ok = true;
#endif
static uint8_t payload_bytes[2048], tx_bytes[96], tx_temp[96];
static size_t payload_size, baseline_size, baseline_temp_size;
static uint8_t baseline_bytes[2048], baseline_temp[2048];

static uint8_t command_bytes[64], feed_bytes[4200], temporary_bytes[4200];
static size_t command_size, feed_size, temporary_size;
static unsigned config_queries, feed_writes, load_result_notes;
static int32_t revision_match = -1;
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
static const char *template_data = "{}";
static const char *probe_data = "controller_mappings { revision 731 }";
static const char *layout_file_data(void) {
    return wcsstr(opened_path, L"madeira_m1_revision_probe.vdf") ? probe_data : template_data;
}
static uint64_t now_ms(void) { return clock_ms; }
static void event(const char *name, int32_t value)
{
    if (!strcmp(name, "steam-input-load-result-events")) ++load_result_notes;
    if (!strcmp(name, "steam-input-load-result-matches-revision")) revision_match = value;
    if (!strncmp(name, "steam-input-output-", 19)) ++output_events;
    if (!strcmp(name, "steam-input-output-changed")) { assert(value == 1); ++output_changes; }
}
static DWORD GetEnvironmentVariableW(const wchar_t *name, wchar_t *out, DWORD cap)
{
    const wchar_t *value = !wcscmp(name, L"MADEIRA_DOCK_STEAM_INPUT") ? L"1" :
        !wcscmp(name, L"MADEIRA_DOCK_INPUT_SESSION") ? (service_enabled ? L"0123456789abcdef0123456789abcdef" : L"") : L"C:\\Steam";
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
    if (wcsstr(path, L"madeira-steam-input-apply-processing.bin")) return payload_claimed ? (HANDLE)5 : INVALID_HANDLE_VALUE;
    if (wcsstr(path, L"madeira-steam-input-tx.tmp")) return (HANDLE)6;
    if (wcsstr(path, L"madeira-steam-input-baseline.tmp")) return (HANDLE)7;
    if (wcsstr(path, L"madeira-steam-input-processing.bin")) return command_claimed ? (HANDLE)3 : INVALID_HANDLE_VALUE;
    if (wcsstr(path, L"madeira-steam-input.tmp")) return (HANDLE)4;
    return template_ok ? (HANDLE)1 : INVALID_HANDLE_VALUE;
}
static DWORD GetFileSize(HANDLE file, void *high) { (void)high; return file == (HANDLE)5 ? (DWORD)payload_size : (DWORD)strlen(layout_file_data()); }
static bool ReadFile(HANDLE file, void *out, DWORD size, DWORD *got, void *overlapped)
{
    (void)overlapped;
    if (file == (HANDLE)5) { assert(size == payload_size); memcpy(out, payload_bytes, size); *got = size; return true; }
    if (file == (HANDLE)3) {
        *got = command_size < size ? (DWORD)command_size : size;
        memcpy(out, command_bytes, *got); return true;
    }
    assert(size == strlen(layout_file_data())); memcpy(out, layout_file_data(), size); *got = size; return true;
}
static bool WriteFile(HANDLE file, const void *data, DWORD size, DWORD *wrote, void *overlapped)
{
    (void)overlapped;
    if (file == (HANDLE)7) { assert(size <= sizeof baseline_temp); memcpy(baseline_temp, data, size); baseline_temp_size = size; *wrote = size; return service_write_ok; }
    if (file == (HANDLE)6) { assert(size == 96); memcpy(tx_temp, data, size); *wrote = size; return service_write_ok; }
    assert(file == (HANDLE)4 && size <= sizeof temporary_bytes);
    memcpy(temporary_bytes, data, size); temporary_size = size; *wrote = size;
    return service_write_ok;
}
static bool MoveFileExW(const wchar_t *from, const wchar_t *to, unsigned flags)
{
    if (wcsstr(from, L"-apply.bin")) {
        assert(wcsstr(to, L"-apply-processing.bin") && flags == MOVEFILE_REPLACE_EXISTING);
        if (!payload_ready) return false;
        payload_ready = false; payload_claimed = true; return true;
    }
    if (wcsstr(from, L"-baseline.tmp")) {
        assert(wcsstr(to, L"-baseline.bin"));
        if (!service_write_ok) return false;
        memcpy(baseline_bytes, baseline_temp, baseline_temp_size); baseline_size = baseline_temp_size; return true;
    }
    if (wcsstr(from, L"-tx.tmp")) {
        assert(wcsstr(to, L"-tx.bin"));
        if (!service_write_ok) return false;
        memcpy(tx_bytes, tx_temp, sizeof tx_bytes); return true;
    }
    if (wcsstr(from, L"-cmd.bin")) {
        assert(wcsstr(to, L"-processing.bin") && flags == MOVEFILE_REPLACE_EXISTING);
        if (!command_ready) return false;
        command_ready = false; command_claimed = true; return true;
    }
    assert(wcsstr(from, L".tmp") && wcsstr(to, L".bin"));
    assert(flags == (MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
    if (!service_write_ok) return false;
    memcpy(feed_bytes, temporary_bytes, temporary_size); feed_size = temporary_size; ++feed_writes; return true;
}
static bool DeleteFileW(const wchar_t *path) {
    if (wcsstr(path, L"-baseline.bin")) baseline_size = 0;
    if (wcsstr(path, L"-apply.bin")) payload_ready = false;
    if (wcsstr(path, L"-apply-processing.bin")) payload_claimed = false;
    if (wcsstr(path, L"-cmd.bin")) command_ready = false;
    if (wcsstr(path, L"-processing.bin")) command_claimed = false;
    return true;
}
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
/* Deterministic hash stub at the CryptoAPI boundary, not a replacement hash
 * implementation. The cross-language tests use actual SHA-256 queue digests. */
#if defined(SH_STEAM_INPUT_LOCAL_APPLY) && SH_STEAM_INPUT_LOCAL_APPLY
static void fixture_hash(const char *data, size_t size, uint8_t out[32])
{
    uint8_t v = 1;
    for (size_t i = 0; i < size; ++i) v = (uint8_t)(v * 33u + (unsigned char)data[i]);
    memset(out, v ? v : 1, 32);
}
static bool verify_fixture_hash(const char *data, size_t size, const uint8_t expected[32])
{ uint8_t actual[32]; fixture_hash(data, size, actual); return hash_ok && !memcmp(actual, expected, 32); }
#define SH_INPUT_VERIFY_HASH verify_fixture_hash
#endif
#include "../src/steam_input.c"

static bool is_connected(void *this, uint32_t index, bool mode)
{ (void)this; assert(index == 0 && !mode); return pad; }
static int32_t count(void *this) { (void)this; ++counts; return pad; }
static bool get_config(void *this, uint32_t index, const char *app, uint64_t *workshop, void *url)
{
    (void)this; ++config_queries; assert(index == 0 && !strcmp(app, "480")); *workshop = personal ? 123 : 0;
    *(const char **)url = personal ? "" : "controller_ps5_gamepad_joystick.vdf";
    return true;
}
static void select_config(void *this, uint32_t index, const struct sh_utl_string *app,
                          uint64_t workshop, const struct sh_utl_string *url, bool a, bool b)
{ (void)this; (void)index; (void)app; (void)workshop; (void)url; (void)a; (void)b; ++selections; }
static void load(void *this, uint32_t index, uint32_t app, const char *vdf, const void *optional,
                 uint64_t id, const struct sh_config_info *info)
{
    (void)this;
    bool expected = !strcmp(vdf, template_data) || !strcmp(vdf, probe_data);
#if defined(SH_STEAM_INPUT_REVISION_PROBE) && SH_STEAM_INPUT_REVISION_PROBE
    expected |= !strcmp(vdf, "controller_mappings { revision \"732\" }") ||
                !strcmp(vdf, "controller_mappings { revision \"733\" }");
#endif
#if defined(SH_STEAM_INPUT_LOCAL_APPLY) && SH_STEAM_INPUT_LOCAL_APPLY
    for (unsigned revision = 732; revision <= 737; ++revision) {
        char value[128]; snprintf(value, sizeof value, "controller_mappings { revision \"%u\" }", revision);
        expected |= !strcmp(vdf, value);
    }
#endif
    assert(index == 0 && app == 480 && expected && !optional && !id);
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

static void command32(size_t at, uint32_t value)
{ for (unsigned i = 0; i < 4; ++i) command_bytes[at + i] = (uint8_t)(value >> (8 * i)); }
static void send_query(const struct sh_steam_input *s, uint32_t sequence, uint32_t action)
{
    memset(command_bytes, 0, sizeof command_bytes); memcpy(command_bytes, "MSICMD01", 8);
    memcpy(command_bytes + 8, s->service.session, 16);
    command32(24, 480); command32(32, s->service.epoch); command32(36, sequence); command32(40, action);
    command_size = SH_INPUT_COMMAND_SIZE; command_ready = true;
}

#if defined(SH_STEAM_INPUT_LOCAL_APPLY) && SH_STEAM_INPUT_LOCAL_APPLY
static void payload_put(unsigned at, uint32_t value)
{ for (unsigned i = 0; i < 4; ++i) payload_bytes[at + i] = (uint8_t)(value >> (8*i)); }
static void local_payload(const struct sh_steam_input *s, uint32_t sequence, uint32_t revision, const char *rollback)
{
    char apply[128]; snprintf(apply, sizeof apply, "controller_mappings { revision \"%u\" }", revision);
    size_t a = strlen(apply), b = strlen(rollback);
    memset(payload_bytes, 0, sizeof payload_bytes); memcpy(payload_bytes, "MSITXN02", 8);
    memcpy(payload_bytes + 8, s->service.session, 16);
    payload_put(24, 480); payload_put(32, s->service.epoch); payload_put(36, sequence);
    payload_put(40, revision); payload_put(44, revision + 1);
    fixture_hash(apply, a, payload_bytes + 48); fixture_hash(rollback, b, payload_bytes + 80);
    payload_put(112, (uint32_t)a); payload_put(116, (uint32_t)b);
    memcpy(payload_bytes + 120, apply, a); memcpy(payload_bytes + 120 + a, rollback, b);
    payload_size = 120 + a + b; payload_ready = true;
}
#endif

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
    /* Run the actual Windows file transport and service through the same
     * lifecycle, using synthetic files and the production pinned call sites. */
    struct sh_steam_input t = {0}; t.o = &o; t.controller = &controller; t.gameid = 480;
    t.last_count = 0; service_enabled = true; clock_ms = 200000;
    sh_steam_input_start(&t);
    assert(feed_size == SH_INPUT_FEED_HEADER && feed_writes == 1 && feed_bytes[52] == 1);
    sh_steam_input_poll(&t); /* pad connection grants lease 1 */
    clock_ms += 500; sh_steam_input_poll(&t);
    assert(t.service.epoch == 1 && t.service.connected && feed_bytes[32] == 1 && feed_bytes[52] == 3);
    unsigned previous_queries = config_queries, previous_loads = loads, previous_activates = activates;
    send_query(&t, 1, SH_INPUT_QUERY); clock_ms += 500; sh_steam_input_poll(&t);
    assert(!command_ready && !command_claimed && t.service.last_sequence == 1 && t.service.result == SH_INPUT_OK);
    assert(config_queries == previous_queries + 1 && t.service.workshop == 123 && feed_bytes[48] == 1);
    send_query(&t, 1, SH_INPUT_QUERY); clock_ms += 500; sh_steam_input_poll(&t);
    assert(config_queries == previous_queries + 1); /* replay never calls Valve */
    send_query(&t, 2, SH_INPUT_APPLY); clock_ms += 500; sh_steam_input_poll(&t);
    assert(t.service.result == SH_INPUT_ACK_UNAVAILABLE && t.service.last_sequence == 2);
    assert(loads == previous_loads && activates == previous_activates && !selections);
    send_query(&t, 3, SH_INPUT_QUERY); command_bytes[8] ^= 1;
    clock_ms += 500; sh_steam_input_poll(&t); assert(t.service.last_sequence == 2);
    send_query(&t, 3, SH_INPUT_QUERY); command_size++;
    clock_ms += 500; sh_steam_input_poll(&t); assert(t.service.last_sequence == 2);
    /* Failed publication retains the dirty snapshot for bounded retry. */
    service_write_ok = false; send_query(&t, 3, SH_INPUT_SOURCES);
    clock_ms += 500; sh_steam_input_poll(&t); assert(t.service_dirty && t.service.last_sequence == 3);
    unsigned previous_writes = feed_writes;
    service_write_ok = true; clock_ms += 500; sh_steam_input_poll(&t);
    assert(!t.service_dirty && feed_writes == previous_writes + 1 && feed_bytes[36] == 3);
    uint8_t notification[40] = {0}; notification[0] = 0xe0; notification[1] = 1;
    notification[8] = 7; notification[24] = 2; notification[28] = 3; notification[33] = 1;
    sh_steam_input_callback(&t, 2802, notification, 40);
    sh_steam_input_callback(&t, 2803, notification, 39);
    notification[0] = 1; sh_steam_input_callback(&t, 2803, notification, 40);
    assert(!t.configuration_events);
    notification[0] = 0xe0;
    for (unsigned i = 0; i < 30; ++i) sh_steam_input_callback(&t, 2803, notification, 40);
    assert(t.configuration_events == 16 && t.configuration.major == 2 && t.configuration.minor == 3);
    assert(t.service.state != SH_INPUT_CACHED && !t.service.mapping_cached);
    uint8_t completion[33] = {0}; completion[0] = 1;
    completion[8] = 0xe0; completion[9] = 1;
    completion[24] = 0xdb; completion[25] = 2; /* document revision 731 */
    memset(completion + 16, 0xff, 8); /* skipped creator identity */
    t.layout_revision_known = true; t.layout_revision = 731;
    sh_steam_input_callback(&t, 1060003, completion, 32);
    assert(!load_result_notes); /* private decoder off for unverified builds */
    t.load_result_supported = true;
    sh_steam_input_callback(&t, 1060003, completion, 31);
    sh_steam_input_callback(&t, 1060003, completion, 33);
    completion[8]++; sh_steam_input_callback(&t, 1060003, completion, 32); completion[8]--;
    completion[4] = 1; sh_steam_input_callback(&t, 1060003, completion, 32); completion[4] = 0;
    assert(!load_result_notes);
    completion[0] = 0; sh_steam_input_callback(&t, 1060003, completion, 32);
    assert(load_result_notes == 1 && !revision_match && !t.load_result.completed);
    completion[0] = 1; completion[24]++; sh_steam_input_callback(&t, 1060003, completion, 32);
    assert(load_result_notes == 2 && !revision_match && t.load_result.completed);
    completion[24]--; sh_steam_input_callback(&t, 1060003, completion, 32);
    assert(load_result_notes == 3 && revision_match == 1 && t.load_result.revision == 731);
    t.layout_revision_known = false; sh_steam_input_callback(&t, 1060003, completion, 32);
    assert(!revision_match);
    uint32_t state_before = t.service.state, result_before = t.service.result;
    for (unsigned i = 0; i < 30; ++i) sh_steam_input_callback(&t, 1060003, completion, 32);
    assert(t.load_result_events == 16 && load_result_notes == 16);
    assert(t.service.state == state_before && t.service.result == result_before);
    puts("PASS: load result build/app/controller gates, truncation, failure/revision matching, report cap and no active-state promotion");
    sh_steam_input_end(&t); assert(!t.service.enabled);
    /* Opt-in experiment shares startup queueing and restores the exact baseline
     * automatically. Ordinary builds reject it even with a valid fixture. */
    struct sh_steam_input u = {0}; u.o = &o; u.controller = &controller; u.gameid = 480;
    u.load_result_supported = u.service.enabled = u.pad_connected = u.layout_queued = true;
    u.layout_revision_known = true; u.layout_revision = 7;
    u.selected_url.text = "controller_ps5_gamepad_joystick.vdf";
    template_data = "controller_mappings { revision 7 }";
#if defined(SH_STEAM_INPUT_REVISION_PROBE) && SH_STEAM_INPUT_REVISION_PROBE
    unsigned baseline_selections = selections;
    /* Reject ambiguous baselines, wrong fixture revisions and overlapping runs. */
    u.selected_workshop = 123; assert(!start_revision_probe(&u)); u.selected_workshop = 0;
    u.layout_revision = INT32_MAX; assert(!start_revision_probe(&u)); u.layout_revision = 7;
    probe_data = "controller_mappings { revision 732 }";
    assert(!start_revision_probe(&u) && !u.probe_original && !u.probe_used);
    probe_data = "controller_mappings { revision 731 }";
    assert(start_revision_probe(&u) && u.probe_phase == 1 && u.layout_revision == 732);
    assert(!strcmp(u.probe_original, template_data) && !start_revision_probe(&u));
    assert(!strcmp(u.probe_restore, "controller_mappings { revision \"733\" }"));
    assert(u.probe_revision == 732 && u.probe_original_revision == 733);
    u.next_poll_ms = clock_ms + 15000;
    completion[24] = 7; completion[25] = 0;
    sh_steam_input_callback(&u, 1060003, completion, 32); assert(u.probe_phase == 1);
    completion[24] = 0xdc; completion[25] = 2;
    sh_steam_input_callback(&u, 1060003, completion, 32);
    assert(u.probe_phase == 2 && u.probe_deadline_ms == clock_ms + 30000);
    poll_revision_probe(&u, clock_ms + 29999); assert(u.probe_phase == 2);
    clock_ms += 30000; poll_revision_probe(&u, clock_ms);
    assert(u.probe_phase == 3 && u.layout_revision == 733 && u.probe_original);
    sh_steam_input_callback(&u, 1060003, completion, 32); assert(u.probe_phase == 3);
    completion[24] = 7; completion[25] = 0; /* delayed startup completion */
    sh_steam_input_callback(&u, 1060003, completion, 32); assert(u.probe_phase == 3);
    completion[24] = 0xdd; completion[25] = 2;
    sh_steam_input_callback(&u, 1060003, completion, 32);
    assert(u.probe_phase == 4 && !u.probe_original && !u.probe_restore && !u.probe_deadline_ms && !start_revision_probe(&u));
    assert(selections == baseline_selections); /* never writes a persisted Steam choice */
    sh_steam_input_end(&u);
    /* No completion still initiates baseline restore, then reports a bounded
     * timeout and retains baseline bytes for cleanup rather than claiming ACK. */
    u.probe_used = false; u.layout_queued = true; u.activate_at_ms = 0;
    u.service.enabled = true; u.layout_revision = 7;
    assert(start_revision_probe(&u)); clock_ms += 15000; poll_revision_probe(&u, clock_ms);
    assert(u.probe_phase == 3 && u.probe_original);
    clock_ms += 15000; poll_revision_probe(&u, clock_ms);
    assert(u.probe_phase == 5 && u.probe_original && !u.probe_deadline_ms);
    sh_steam_input_end(&u); assert(!u.probe_original);
    puts("PASS: opt-in one-shot native revision probe, overlap rejection, revision-tagged restore, stale startup rejection, timeout and cleanup");
#else
    assert(!start_revision_probe(&u) && !u.probe_original && !u.probe_used);
    puts("PASS: ordinary builds cannot execute the native revision probe");
#endif
    template_data = "{}";
#if defined(SH_STEAM_INPUT_LOCAL_APPLY) && SH_STEAM_INPUT_LOCAL_APPLY
    struct sh_steam_input live = {0}; live.o = &o; live.controller = &controller; live.gameid = 480;
    live.load_result_supported = live.pad_connected = live.sdl_started = true;
    live.next_poll_ms = clock_ms + 1; live.next_controller_ms = UINT64_MAX; live.last_count = 0;
    live.service.enabled = live.service.available = live.service.connected = live.service.local_apply = live.service.working_snapshot = true;
    live.service.appid = 480; live.service.epoch = 1;
    assert(sh_input_session_parse("0123456789abcdef0123456789abcdef", live.service.session));
    template_data = "controller_mappings { revision 7 }";
    queue_layout_bytes(&live, template_data); live.activate_at_ms = 0;
    live.load_result = (struct sh_input_load_result){.appid=480,.revision=7,.completed=true};
    /* Retained working bytes are a valid queue source too; don't free them
     * before the native call has consumed that same pointer. */
    queue_layout_bytes(&live, live.current_layout); live.activate_at_ms = 0;
    live.service.state = SH_INPUT_ACTIVATION_REQUESTED;
    clock_ms += 500; sh_steam_input_poll(&live);
    struct sh_input_baseline exported;
    assert(baseline_size && sh_input_baseline_parse(baseline_bytes, baseline_size, &exported) && exported.revision == 7);
    unsigned before_loads = loads, before_activates = activates;
    local_payload(&live, 1, 732, "controller_mappings { revision \"733\" }"); hash_ok = false;
    send_query(&live, 1, SH_INPUT_APPLY); clock_ms += 500; sh_steam_input_poll(&live);
    assert(live.service.result == SH_INPUT_BAD_COMMAND && loads == before_loads && !live.transaction.sequence); hash_ok = true;
    local_payload(&live, 2, 732, "controller_mappings { revision \"733\" wrong baseline }");
    send_query(&live, 2, SH_INPUT_APPLY); clock_ms += 500; sh_steam_input_poll(&live);
    assert(live.service.result == SH_INPUT_BAD_COMMAND && loads == before_loads);
    local_payload(&live, 3, 732, "controller_mappings { revision \"733\" }");
    send_query(&live, 3, SH_INPUT_APPLY); clock_ms += 500; sh_steam_input_poll(&live);
    assert(live.service.result == SH_INPUT_OK && loads == before_loads + 1 && live.transaction.phase == SH_INPUT_TX_APPLYING);
    assert(!baseline_size);
    send_query(&live, 4, SH_INPUT_APPLY); clock_ms += 500; sh_steam_input_poll(&live);
    assert(live.service.result == SH_INPUT_BUSY && live.transaction.sequence == 3);
    clock_ms += 3000; sh_steam_input_poll(&live);
    assert(activates == before_activates && live.transaction.phase == SH_INPUT_TX_APPLYING);
    memset(completion, 0, sizeof completion); completion[0] = 1; completion[8] = 0xe0; completion[9] = 1;
    completion[24] = 0xdc; completion[25] = 2;
    sh_steam_input_callback(&live, 1060003, completion, 32);
    clock_ms += 101; sh_steam_input_poll(&live); clock_ms += 500; sh_steam_input_poll(&live);
    assert(live.transaction.phase == SH_INPUT_TX_CONFIRMED && live.service.state == SH_INPUT_ACTIVE);
    assert(tx_bytes[40] == SH_INPUT_TX_CONFIRMED && tx_bytes[92] == 1 && activates == before_activates + 1);
    assert(sh_input_baseline_parse(baseline_bytes, baseline_size, &exported) && exported.revision == 732);
    completion[24] = 7; completion[25] = 0;
    sh_steam_input_callback(&live, 1060003, completion, 32); /* late startup diagnostic cannot invalidate a confirmed receipt */
    completion[24] = 0xdc; completion[25] = 2;
    /* Timeout restores the verified working bytes with a fresh revision. */
    local_payload(&live, 5, 734, "controller_mappings { revision \"735\" }");
    send_query(&live, 5, SH_INPUT_APPLY); clock_ms += 500; sh_steam_input_poll(&live);
    assert(live.service.result == SH_INPUT_OK);
    clock_ms += 15000; sh_steam_input_poll(&live);
    assert(live.transaction.phase == SH_INPUT_TX_ROLLING_BACK && live.layout_revision == 735);
    sh_steam_input_callback(&live, 1060003, completion, 32); assert(!live.transaction.completed);
    completion[24] = 0xde; sh_steam_input_callback(&live, 1060003, completion, 32); assert(!live.transaction.completed);
    completion[24] = 0xdf; sh_steam_input_callback(&live, 1060003, completion, 32);
    clock_ms += 3001; sh_steam_input_poll(&live); clock_ms += 500; sh_steam_input_poll(&live);
    assert(live.transaction.phase == SH_INPUT_TX_ROLLED_BACK && live.transaction.working_known && tx_bytes[92] == 1);
    local_payload(&live, 6, 736, "controller_mappings { revision \"737\" }");
    send_query(&live, 6, SH_INPUT_APPLY); clock_ms += 500; sh_steam_input_poll(&live);
    clock_ms += 15000; sh_steam_input_poll(&live);
    clock_ms += 15000; sh_steam_input_poll(&live);
    assert(live.transaction.phase == SH_INPUT_TX_FAILED && !live.transaction.working_known && !live.activate_at_ms);
    assert(live.service.state == SH_INPUT_FAILED && tx_bytes[92] == 0);
    send_query(&live, 7, SH_INPUT_APPLY); clock_ms += 500; sh_steam_input_poll(&live);
    assert(live.service.result == SH_INPUT_UNAVAILABLE);
    sh_steam_input_end(&live);
    assert(!live.current_layout && !live.transaction_apply && !live.transaction_rollback && !baseline_size);
    puts("PASS: live local submission gates, digest/baseline verification, serialized apply, exact completion before activation, rollback/late callbacks, receipt feed and failure cleanup");
#endif
    /* An unavailable adapter still gives the app a scoped explanation. */
    struct sh_steam_input unavailable = {0}; unavailable.o = &o; unavailable.gameid = 480;
    sh_steam_input_start(&unavailable); send_query(&unavailable, 1, SH_INPUT_QUERY);
    clock_ms += 500; sh_steam_input_poll(&unavailable);
    assert(unavailable.service.result == SH_INPUT_UNAVAILABLE && !unavailable.service.available);
    sh_steam_input_end(&unavailable);
    puts("PASS: session service atomic publication/claim, query, replay, invalid input, unavailable adapter and failed-write retry");
    puts("PASS: Steam selection keys/autosaves, path bounds, late pad, SDL retry, delayed cache activation, refocus, reconnect and personal selection");
    puts("PASS: output diagnostic retries and cleanup, read-only bounded views, packet-only changes and disconnected state");
}
