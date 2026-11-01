/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md */
#ifndef MADEIRA_DOCK_STEAM_INPUT_H
#define MADEIRA_DOCK_STEAM_INPUT_H
#include "session.h"

/* Steam Input during a launched game. Only with MADEIRA_DOCK_STEAM_INPUT=1.
 * When unavailable (unpinned client build, a method mismatch) it reports
 * steam-input-available=0 and does nothing else; the launch is never
 * affected. Reports numbers only. */
struct sh_steam_input {
    void *controller; /* IClientControllerSerialized, or NULL */
    const struct sh_observer *o;
    uint64_t next_poll_ms, next_sdl_ms;
    int32_t last_count;
    bool sdl_started; /* SDL's gamepad subsystem started (or tried) */
    /* IClientUtils, or NULL: the focused game, as desktop Steam's UI reports
     * it. focus_pid is the game process last reported. */
    void *utils;
    uint64_t gameid, next_focus_ms, activate_at_ms;
    DWORD focus_pid;
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
                            void *client_user, uint32_t appid, const struct dock_client_layout *layout,
                            const struct sh_observer *o);
/* After LaunchApp: the one question is asked SH_STEAM_INPUT_DELAY_MS later. */
void sh_steam_input_start(struct sh_steam_input *s);
/* Call from the launch loop. */
void sh_steam_input_poll(struct sh_steam_input *s);
void sh_steam_input_end(struct sh_steam_input *s);
#endif
