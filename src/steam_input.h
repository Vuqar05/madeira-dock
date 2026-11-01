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
    uint64_t next_poll_ms;
    int32_t last_count;
    /* SteamController_PSSupport as found, restored at the end; -1 when unchanged. */
    void *user;
    int32_t saved_ps_support;
};

/* Before LaunchApp: Steam Input for PlayStation pads in every game ("2"), as
 * desktop Steam's setting does, then the client is told the setting changed,
 * so this launch's opt-in includes them. The previous value comes back in
 * sh_steam_input_end. */
void sh_steam_input_prepare(struct sh_steam_input *s, HMODULE module, void *engine, int32_t pipe,
                            void *client_user, const struct dock_client_layout *layout,
                            const struct sh_observer *o);
/* After LaunchApp: the one question is asked SH_STEAM_INPUT_DELAY_MS later. */
void sh_steam_input_start(struct sh_steam_input *s);
/* Call from the launch loop. */
void sh_steam_input_poll(struct sh_steam_input *s);
void sh_steam_input_end(struct sh_steam_input *s);
#endif
