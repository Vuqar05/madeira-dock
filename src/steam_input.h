/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md */
#ifndef MADEIRA_DOCK_STEAM_INPUT_H
#define MADEIRA_DOCK_STEAM_INPUT_H
#include "session.h"

/* Steam Input probe during a launched game: reports whether Valve's client
 * controller manager sees a controller. Optional: when unavailable (unpinned
 * client build, MADEIRA_DOCK_STEAM_INPUT=0 or unset, a method mismatch) it
 * reports steam-input-available=0 and does nothing else; the launch is never
 * affected. Reports numbers only. */
struct sh_steam_input {
    void *controller; /* IClientControllerSerialized, or NULL */
    const struct sh_observer *o;
    uint64_t next_poll_ms;
    int32_t last_count;
};

void sh_steam_input_init(struct sh_steam_input *s, HMODULE module, void *engine, int32_t pipe,
                         const struct dock_client_layout *layout, const struct sh_observer *o);
/* Asks once, SH_STEAM_INPUT_DELAY_MS after init; call from the launch loop. */
void sh_steam_input_poll(struct sh_steam_input *s);
#endif
