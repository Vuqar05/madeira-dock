/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md */
#ifndef MADEIRA_DOCK_STEAM_INPUT_SERVICE_H
#define MADEIRA_DOCK_STEAM_INPUT_SERVICE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SH_INPUT_COMMAND_SIZE 44u
#define SH_INPUT_KEY_CAP 4096u
#define SH_INPUT_FEED_HEADER 72u
#define SH_INPUT_FEED_CAP (SH_INPUT_FEED_HEADER + SH_INPUT_KEY_CAP)
/* Explicit LE wire encoding; never serialize a native struct. No command
 * accepts a path. Ordinary builds reserve apply/reset; a gated local apply
 * uses a separate bounded, hash-bound envelope and transaction receipt. */
enum sh_input_action { SH_INPUT_QUERY = 1, SH_INPUT_SOURCES, SH_INPUT_APPLY, SH_INPUT_DEFAULT, SH_INPUT_REVISION_PROBE };
enum sh_input_result { SH_INPUT_OK = 1, SH_INPUT_STALE, SH_INPUT_UNSUPPORTED,
    SH_INPUT_UNAVAILABLE, SH_INPUT_NO_CONTROLLER, SH_INPUT_BAD_COMMAND, SH_INPUT_ACK_UNAVAILABLE, SH_INPUT_BUSY };
enum sh_input_state { SH_INPUT_IDLE, SH_INPUT_DISCONNECTED, SH_INPUT_QUEUED,
    SH_INPUT_CACHED, SH_INPUT_ACTIVATION_REQUESTED, SH_INPUT_FAILED, SH_INPUT_ACTIVE };
/* These are transport input groups, not a claim about physical hardware.
 * No motion, touch coordinates, rear buttons or Deck/Steam Controller pads. */
#define SH_INPUT_FORWARDED 15u /* buttons=1, sticks=2, triggers=4, touch click=8 */
struct sh_input_command {
    uint8_t session[16];
    uint32_t appid, controller, epoch, sequence, action;
};
struct sh_input_service {
    uint8_t session[16];
    uint32_t appid, epoch, last_sequence, result, state;
    bool enabled, available, connected, mapping_cached, selected_fallback, local_apply, working_snapshot;
    uint64_t workshop;
    char selection[SH_INPUT_KEY_CAP + 1];
};
/* SteamInputConfigurationLoaded_t, public callback 2803, January x64 ABI:
 * app(4), padding(4), device(8), creator(8), major/minor(4 each), two bools,
 * tail padding. Creator is deliberately neither decoded nor retained. The
 * revisions describe the action manifest, not a layout content hash. */
struct sh_input_configuration {
    uint32_t appid, major, minor;
    uint64_t device;
    bool native_actions, gamepad_output;
};
/* January LoadControllerConfigResult_t (1060003), exactly 32 bytes.
 * Decode only completed/controller/app/document revision. The creator at
 * offset 16 and unrelated metadata are deliberately skipped. */
struct sh_input_load_result {
    uint32_t controller, appid, revision;
    bool completed;
};
bool sh_input_load_result_parse(const void *data, size_t size, struct sh_input_load_result *out);
/* Read the native document revision without changing any bytes. Unknown or
 * ambiguous documents return false; an absent revision in a valid mapping is 0. */
bool sh_input_vdf_revision(const char *data, size_t size, uint32_t *revision);
/* Prepare a private queue copy, replacing only the root revision token (or
 * inserting it when absent). All other bytes, including unknown bindings, are
 * preserved. Source and output must not overlap. Failure leaves output and
 * written unchanged; success includes a trailing NUL outside written bytes.
 * This validates metadata structure only, not binding semantics. */
bool sh_input_vdf_materialize(const char *data, size_t size, uint32_t revision,
                              char *out, size_t cap, size_t *written);
bool sh_input_configuration_parse(const void *data, size_t size, struct sh_input_configuration *out);
bool sh_input_session_parse(const char *hex, uint8_t out[16]);
bool sh_input_command_parse(const uint8_t *data, size_t size, struct sh_input_command *out);
/* Scope failures do not consume a sequence; duplicate/older sequences never
 * execute again. Caller queries Valve only after OK on QUERY or SOURCES. */
uint32_t sh_input_service_accept(struct sh_input_service *s, const struct sh_input_command *c);
void sh_input_service_connection(struct sh_input_service *s, bool connected);
size_t sh_input_service_feed(const struct sh_input_service *s, uint8_t *out, size_t cap);
/* Pure transaction coordinator. No file access or native calls. Callers must
 * validate/hash immutable queue bytes and durably reserve high_water before
 * queueing; this model alone is not an enabled apply command. Keep one instance
 * per launch/controller and preserve high_water across reconnect/cancellation. */
enum sh_input_tx_phase { SH_INPUT_TX_IDLE, SH_INPUT_TX_PREPARED, SH_INPUT_TX_APPLYING,
    SH_INPUT_TX_CONFIRMED, SH_INPUT_TX_ROLLING_BACK, SH_INPUT_TX_ROLLED_BACK,
    SH_INPUT_TX_FAILED, SH_INPUT_TX_CANCELLED };
struct sh_input_receipt {
    uint32_t appid, epoch, sequence, revision;
    uint8_t hash[32]; /* digest of the exact revision-tagged queue bytes */
};
struct sh_input_transaction {
    enum sh_input_tx_phase phase;
    uint32_t appid, epoch, sequence, high_water;
    uint32_t apply_revision, rollback_revision;
    uint8_t apply_hash[32], rollback_hash[32];
    uint64_t deadline;
    bool bound, completed, activation_requested, working_known;
    struct sh_input_receipt working; /* retained on failure, never relabelled active */
};
/* Initialize from a durable revision floor before issuing work. Zero app/epoch
 * or floor outside signed native revision range is rejected without mutation. */
bool sh_input_tx_init(struct sh_input_transaction *t, uint32_t appid, uint32_t epoch, uint32_t floor);
/* Busy/stale/invalid/exhausted requests do not change any state. Reserve two
 * revisions above floor and source_floor; returned revisions must be used to
 * materialize and hash both copies BEFORE bind. No caller supplied paths. */
bool sh_input_tx_reserve(struct sh_input_transaction *t, uint32_t epoch, uint32_t sequence, uint32_t source_floor);
bool sh_input_tx_bind(struct sh_input_transaction *t, const uint8_t apply_hash[32], const uint8_t rollback_hash[32]);
/* Invoke only after the reserved floor/journal and both immutable copies are
 * durable, immediately before submitting the apply copy to the native client. */
bool sh_input_tx_queue(struct sh_input_transaction *t, uint64_t now);
/* Cache hits do not advance this state machine. Confirmation needs BOTH a
 * matching private completed result and the activation request for that queue. */
bool sh_input_tx_activated(struct sh_input_transaction *t, uint32_t revision, uint64_t now);
bool sh_input_tx_completion(struct sh_input_transaction *t, const struct sh_input_load_result *result, uint64_t now);
/* true means caller must now submit the prebuilt rollback copy exactly once.
 * Rollback timeout leaves working_known=false, preserving only historical receipt. */
bool sh_input_tx_poll(struct sh_input_transaction *t, uint64_t now);
bool sh_input_tx_fail(struct sh_input_transaction *t, uint64_t now);
/* New epoch invalidates observations, not revision/sequence high-water marks.
 * Caller owns native recovery/cleanup; cancellation is never an acknowledgement. */
bool sh_input_tx_disconnect(struct sh_input_transaction *t, uint32_t new_epoch);

/* Local apply envelope, independent of v1 query commands. No paths or keys:
 * MSITXN02, scope, revisions, exact queue hashes/lengths, then raw VDF bytes.
 * Parse validates framing/metadata; caller MUST verify both cryptographic
 * digests before binding/submitting. Borrowed spans live as long as data. */
#define SH_INPUT_APPLY_HEADER 120u
#define SH_INPUT_LAYOUT_LIMIT (1u << 20)
#define SH_INPUT_APPLY_LIMIT (SH_INPUT_APPLY_HEADER + 2u * (SH_INPUT_LAYOUT_LIMIT - 1u))
#define SH_INPUT_TX_FEED_SIZE 96u
struct sh_input_apply {
    struct sh_input_command scope;
    uint32_t apply_revision, rollback_revision;
    uint8_t apply_hash[32], rollback_hash[32];
    const char *apply, *rollback;
    size_t apply_size, rollback_size;
};
bool sh_input_apply_parse(const uint8_t *data, size_t size, struct sh_input_apply *out);
bool sh_input_apply_scope(const struct sh_input_apply *apply, const struct sh_input_command *command);
size_t sh_input_tx_feed(const struct sh_input_service *service, const struct sh_input_transaction *transaction,
                        uint8_t *out, size_t cap);
/* Atomic working-layout snapshot for startup reapplication. The app validates
 * scope and metadata, then hashes the exact bytes itself. No file paths. */
#define SH_INPUT_BASELINE_HEADER 44u
struct sh_input_baseline {
    uint8_t session[16];
    uint32_t appid, epoch, revision;
    const char *data;
    size_t size;
};
size_t sh_input_baseline_feed(const struct sh_input_service *service, uint32_t revision,
    const char *data, size_t size, uint8_t *out, size_t cap);
bool sh_input_baseline_parse(const uint8_t *data, size_t size, struct sh_input_baseline *out);
#endif
