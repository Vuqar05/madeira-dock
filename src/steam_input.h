/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md */
#ifndef MADEIRA_DOCK_STEAM_INPUT_H
#define MADEIRA_DOCK_STEAM_INPUT_H
#include "session.h"
#include "steam_input_service.h"

/* Steam Input during a launched game. Only with MADEIRA_DOCK_STEAM_INPUT=1.
 * When unavailable (unpinned client build, a method mismatch) it reports
 * steam-input-available=0 and does nothing else; the launch is never
 * affected. Logs numbers only; the optional session-bound local query feed
 * contains the selection key and Workshop identity (never logged). */
struct sh_steam_input {
    struct sh_input_service service;
    struct sh_input_transaction transaction;
    bool transaction_dirty, baseline_dirty;
    uint64_t next_transaction_ms, next_baseline_ms;
    char *current_layout, *transaction_apply, *transaction_rollback;
    uint64_t next_service_ms;
    bool service_dirty;
    bool load_result_supported, layout_revision_known;
    uint32_t layout_revision;
    unsigned probe_phase;
    bool probe_used;
    uint64_t probe_deadline_ms;
    char *probe_original; /* diagnostic only; exact baseline bytes, never a Steam selection write */
    char *probe_restore; /* private revision-tagged copy; original stays untouched */
    uint32_t probe_revision, probe_original_revision;
    unsigned load_result_events;
    struct sh_input_load_result load_result; /* no creator identity */
    unsigned configuration_events;
    struct sh_input_configuration configuration; /* device handle remains private */
    void *controller; /* IClientControllerSerialized, or NULL */
    const struct sh_observer *o;
    uint64_t next_poll_ms, next_sdl_ms;
    int32_t last_count;
    bool sdl_started; /* SDL's gamepad subsystem started (or tried) */
    /* IClientUtils, or NULL: the focused game, as desktop Steam's UI reports
     * it. focus_pid is the game process last reported. */
    void *utils;
    uint64_t gameid, next_focus_ms, activate_at_ms;
    uint32_t account_id;
    DWORD focus_pid;
    bool pad_connected, layout_queued;
    unsigned sdl_attempts, layout_attempts, cache_waits;
    uint64_t next_controller_ms;
    /* Read-only observation of Valve's January XInput output stream. */
    HANDLE output_map;
    const uint8_t *output_view;
    uint64_t next_output_ms;
    int32_t output_reserved, output_connected;
    uint8_t output_state[12];
    bool output_changed;
    /* CUtlString storage owned/reused by the client for the whole session. */
    struct { const char *text; } selected_url;
    uint64_t selected_workshop;
    /* The per-game override and the PlayStation setting as found in stores 1
     * to 4, restored at the end where changed_override has their bit (two bits
     * per store: override, PlayStation setting). */
    void *user;
    char override_key[40];
    int32_t saved_override[4], saved_ps[4];
    uint32_t changed_override;
};

/* Before LaunchApp: the game's Steam Input override goes to "forced on" and
 * the PlayStation setting to "every game", as desktop Steam's settings do,
 * then the client is told the settings changed. The previous values come back
 * in sh_steam_input_end. */
void sh_steam_input_prepare(struct sh_steam_input *s, HMODULE module, void *engine, int32_t pipe,
                            void *client_user, uint64_t steamid, uint32_t appid, const struct dock_client_layout *layout,
                            const struct sh_observer *o);
/* After LaunchApp: the one question is asked SH_STEAM_INPUT_DELAY_MS later. */
void sh_steam_input_start(struct sh_steam_input *s);
/* Call from the launch loop. */
void sh_steam_input_poll(struct sh_steam_input *s);
/* Observe the public configuration-loaded callback; never an exact-revision ACK. */
void sh_steam_input_callback(struct sh_steam_input *s, int32_t id, const void *data, int32_t size);
void sh_steam_input_end(struct sh_steam_input *s);
#endif
