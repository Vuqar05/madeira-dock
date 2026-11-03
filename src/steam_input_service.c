/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md */
#include "steam_input_service.h"
#include <string.h>
#include <stdio.h>
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void put64(uint8_t *p, uint64_t v)
{ for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
bool sh_input_load_result_parse(const void *data, size_t size, struct sh_input_load_result *out)
{
    if (!data || !out || size != 32) return false;
    const uint8_t *bytes = data;
    struct sh_input_load_result value = {0};
    value.controller = get32(bytes + 4); value.appid = get32(bytes + 8);
    value.revision = get32(bytes + 24);
    if (bytes[0] > 1 || value.controller >= 16 || !value.appid) return false;
    value.completed = bytes[0] != 0;
    *out = value;
    return true;
}

/* Small bounded VDF reader for metadata only. Unknown bindings and repeated
 * keys are not transformed. The original layout still goes to Valve intact. */
struct revision_reader { const char *text; size_t size, at; unsigned tokens; bool truncated; size_t start, revision_start, revision_end, root_open; };
/* Token: 1 string, 2 open, 3 close, 0 EOF, -1 malformed. */
static int revision_token(struct revision_reader *r, char *text, size_t cap)
{
    size_t n = 0;
    r->truncated = false;
    if (++r->tokens > 100000) return -1;
    for (;;) {
        while (r->at < r->size && (r->text[r->at] == ' ' || r->text[r->at] == '\t' ||
               r->text[r->at] == '\r' || r->text[r->at] == '\n')) ++r->at;
        if (r->at + 1 < r->size && r->text[r->at] == '/' && r->text[r->at + 1] == '/') {
            while (r->at < r->size && r->text[r->at] != '\n') ++r->at;
        } else break;
    }
    if (r->at == r->size) return 0;
    r->start = r->at;
    unsigned char c = (unsigned char)r->text[r->at++];
    if (c == '{') return 2;
    if (c == '}') return 3;
    bool quoted = c == '"';
    if (!quoted) --r->at;
    bool closed = !quoted;
    while (r->at < r->size) {
        c = (unsigned char)r->text[r->at];
        if (quoted && c == '"') { ++r->at; closed = true; break; }
        if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '{' || c == '}')) break;
        ++r->at;
        if (c < 32 || c == 127) return -1;
        if (quoted && c == '\\') {
            if (r->at == r->size) return -1;
            c = (unsigned char)r->text[r->at++];
            /* Accept Valve's ordinary quoted escapes, but never treat an
             * escaped representation as an exact metadata key. */
            if (c != '\\' && c != '"' && c != 'n' && c != 'r' && c != 't') return -1;
            if (n + 1 < cap) text[n] = '\\';
            ++n;
        }
        if (n + 1 < cap) text[n] = (char)c;
        ++n;
        if (n > 4096) return -1;
    }
    if (!closed || (!quoted && !n)) return -1;
    r->truncated = n >= cap;
    text[n < cap ? n : cap - 1] = 0;
    return 1;
}
/* KeyValues consumers may compare keys without case. Reject noncanonical
 * spelling rather than overlook metadata that could win in the native parser. */
static bool revision_key(const char *key)
{
    const char *expected = "revision";
    for (; *key && *expected; ++key, ++expected) {
        unsigned char c = (unsigned char)*key;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
        if (c != (unsigned char)*expected) return false;
    }
    return !*key && !*expected;
}
static bool revision_block(struct revision_reader *r, unsigned depth, uint32_t *revision, bool *seen)
{
    if (depth > 64) return false;
    char key[32], value[32];
    for (;;) {
        int token = revision_token(r, key, sizeof key);
        if (token == 3) return true;
        if (token != 1) return false;
        token = revision_token(r, value, sizeof value);
        bool metadata = depth == 1 && revision_key(key);
        if (metadata && strcmp(key, "revision")) return false;
        if (token == 2) {
            if (metadata || !revision_block(r, depth + 1, revision, seen)) return false;
        } else if (token == 1) {
            if (metadata) {
                if (*seen || !*value || r->truncated) return false;
                uint32_t number = 0;
                for (const char *p = value; *p; ++p) {
                    if (*p < '0' || *p > '9' || number > (INT32_MAX - (uint32_t)(*p - '0')) / 10) return false;
                    number = number * 10 + (uint32_t)(*p - '0');
                }
                *seen = true; *revision = number;
                r->revision_start = r->start; r->revision_end = r->at;
            }
        } else return false;
    }
}
static bool revision_document(struct revision_reader *reader, uint32_t *value)
{
    char token[32]; bool seen = false;
    if (revision_token(reader, token, sizeof token) != 1 || strcmp(token, "controller_mappings") ||
        revision_token(reader, token, sizeof token) != 2) return false;
    reader->root_open = reader->at;
    return revision_block(reader, 1, value, &seen) && revision_token(reader, token, sizeof token) == 0;
}
bool sh_input_vdf_revision(const char *data, size_t size, uint32_t *revision)
{
    if (!data || !revision || !size || size >= (1u << 20) || memchr(data, 0, size)) return false;
    struct revision_reader reader = {.text = data, .size = size};
    uint32_t value = 0;
    if (!revision_document(&reader, &value)) return false;
    *revision = value;
    return true;
}
bool sh_input_vdf_materialize(const char *data, size_t size, uint32_t revision,
                              char *out, size_t cap, size_t *written)
{
    if (!data || !out || !written || !size || size >= (1u << 20) ||
        !revision || revision > INT32_MAX || memchr(data, 0, size)) return false;
    struct revision_reader reader = {.text = data, .size = size};
    uint32_t previous = 0;
    if (!revision_document(&reader, &previous)) return false;
    char replacement[48];
    bool existing = reader.revision_end != 0;
    int count = snprintf(replacement, sizeof replacement, existing ? "\"%u\"" : "\n\"revision\" \"%u\"\n", revision);
    if (count <= 0 || (size_t)count >= sizeof replacement) return false;
    size_t begin = existing ? reader.revision_start : reader.root_open;
    size_t end = existing ? reader.revision_end : reader.root_open;
    size_t length = size - (end - begin) + (size_t)count;
    if (length >= (1u << 20) || cap <= length) return false;
    /* Check only the output range actually written. Use integer differences
     * to avoid relational pointer comparisons between unrelated objects. */
    uintptr_t src = (uintptr_t)data, dst = (uintptr_t)out;
    if ((dst >= src && dst - src < size) || (src >= dst && src - dst <= length)) return false;
    memcpy(out, data, begin);
    memcpy(out + begin, replacement, (size_t)count);
    memcpy(out + begin + count, data + end, size - end);
    out[length] = 0; *written = length;
    return true;
}

bool sh_input_configuration_parse(const void *data, size_t size, struct sh_input_configuration *out)
{
    if (!data || !out || size != 40) return false;
    const uint8_t *bytes = data;
    struct sh_input_configuration value = {0};
    value.appid = get32(bytes);
    value.device = (uint64_t)get32(bytes + 8) | (uint64_t)get32(bytes + 12) << 32;
    value.major = get32(bytes + 24); value.minor = get32(bytes + 28);
    if (!value.appid || !value.device || value.device == UINT64_MAX || bytes[32] > 1 || bytes[33] > 1) return false;
    value.native_actions = bytes[32] != 0; value.gamepad_output = bytes[33] != 0;
    *out = value;
    return true;
}
bool sh_input_session_parse(const char *hex, uint8_t out[16])
{
    if (!hex || strlen(hex) != 32) return false;
    uint8_t bytes[16] = {0};
    bool nonzero = false;
    for (unsigned i = 0; i < 32; ++i) {
        unsigned c = (unsigned char)hex[i], v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else return false;
        bytes[i / 2] |= (uint8_t)(v << (i % 2 ? 0 : 4));
        nonzero |= v != 0;
    }
    if (!nonzero) return false;
    memcpy(out, bytes, 16);
    return true;
}
bool sh_input_command_parse(const uint8_t *data, size_t size, struct sh_input_command *out)
{
    if (!data || !out || size != SH_INPUT_COMMAND_SIZE || memcmp(data, "MSICMD01", 8)) return false;
    struct sh_input_command c = {0};
    memcpy(c.session, data + 8, 16);
    c.appid = get32(data + 24); c.controller = get32(data + 28);
    c.epoch = get32(data + 32); c.sequence = get32(data + 36); c.action = get32(data + 40);
    if (!c.appid || !c.sequence || c.action < SH_INPUT_QUERY || c.action > SH_INPUT_REVISION_PROBE) return false;
    *out = c;
    return true;
}
uint32_t sh_input_service_accept(struct sh_input_service *s, const struct sh_input_command *c)
{
    if (!s->enabled || memcmp(s->session, c->session, 16) || s->appid != c->appid ||
        c->controller != 0 || s->epoch != c->epoch || !c->sequence || c->sequence <= s->last_sequence)
        return SH_INPUT_STALE;
    s->last_sequence = c->sequence;
    if (!s->available) return s->result = SH_INPUT_UNAVAILABLE;
    if (c->action == SH_INPUT_APPLY || c->action == SH_INPUT_DEFAULT || c->action == SH_INPUT_REVISION_PROBE)
        return s->result = SH_INPUT_ACK_UNAVAILABLE;
    if (c->action != SH_INPUT_QUERY && c->action != SH_INPUT_SOURCES)
        return s->result = SH_INPUT_UNSUPPORTED;
    if (!s->connected) return s->result = SH_INPUT_NO_CONTROLLER;
    return s->result = SH_INPUT_OK;
}
void sh_input_service_connection(struct sh_input_service *s, bool connected)
{
    if (connected == s->connected) return;
    /* Never wrap a lease and permit an old command to match it. */
    if (s->epoch == UINT32_MAX) { s->enabled = false; return; }
    ++s->epoch;
    s->connected = connected;
    s->workshop = 0; s->selection[0] = 0; s->mapping_cached = false;
    s->state = connected ? SH_INPUT_IDLE : SH_INPUT_DISCONNECTED;
}
size_t sh_input_service_feed(const struct sh_input_service *s, uint8_t *out, size_t cap)
{
    size_t length = 0;
    while (length <= SH_INPUT_KEY_CAP && s->selection[length]) ++length;
    if (!s->enabled || length > SH_INPUT_KEY_CAP || cap < SH_INPUT_FEED_HEADER + length) return 0;
    memcpy(out, "MSIFEED1", 8); memcpy(out + 8, s->session, 16);
    put32(out + 24, s->appid); put32(out + 28, 0); put32(out + 32, s->epoch);
    put32(out + 36, s->last_sequence); put32(out + 40, s->result); put32(out + 44, s->state);
    /* Sources: bit 0 is an existing Steam selection. No recommendation,
     * Workshop download, personal-layout registry or template listing yet. */
    put32(out + 48, (s->selection[0] || s->workshop) ? 1 : 0);
    /* Flags: available adapter, connected controller, generic cache presence.
     * Bit 3 marks a generic fallback selected by Dock during this session;
     * it must not be migrated as a pre-existing Steam choice. None is an ACK. */
    put32(out + 52, (s->available ? 1u : 0u) | (s->connected ? 2u : 0u) | (s->mapping_cached ? 4u : 0u) | (s->selected_fallback ? 8u : 0u) | (s->local_apply ? 16u : 0u) | (s->working_snapshot ? 32u : 0u));
    put64(out + 56, s->workshop); put32(out + 64, SH_INPUT_FORWARDED);
    put32(out + 68, (uint32_t)length); memcpy(out + SH_INPUT_FEED_HEADER, s->selection, length);
    return SH_INPUT_FEED_HEADER + length;
}

#define SH_INPUT_TX_TIMEOUT 15000u
static bool tx_busy(const struct sh_input_transaction *t)
{
    return t->phase == SH_INPUT_TX_PREPARED || t->phase == SH_INPUT_TX_APPLYING ||
           t->phase == SH_INPUT_TX_ROLLING_BACK;
}
bool sh_input_tx_init(struct sh_input_transaction *t, uint32_t appid, uint32_t epoch, uint32_t floor)
{
    if (!t || !appid || !epoch || floor > INT32_MAX) return false;
    struct sh_input_transaction value = {0};
    value.appid = appid; value.epoch = epoch; value.high_water = floor;
    *t = value;
    return true;
}
bool sh_input_tx_reserve(struct sh_input_transaction *t, uint32_t epoch, uint32_t sequence, uint32_t source_floor)
{
    if (!t || !t->appid || !t->epoch || epoch != t->epoch || !sequence ||
        sequence <= t->sequence || tx_busy(t) || t->phase == SH_INPUT_TX_FAILED) return false;
    uint32_t floor = t->high_water > source_floor ? t->high_water : source_floor;
    if (floor > INT32_MAX - 2) return false;
    t->sequence = sequence;
    t->apply_revision = floor + 1; t->rollback_revision = floor + 2;
    t->high_water = floor + 2;
    memset(t->apply_hash, 0, sizeof t->apply_hash);
    memset(t->rollback_hash, 0, sizeof t->rollback_hash);
    t->bound = t->completed = t->activation_requested = false;
    t->deadline = 0; t->phase = SH_INPUT_TX_PREPARED;
    return true;
}
static bool tx_hash_valid(const uint8_t hash[32])
{
    if (!hash) return false;
    uint8_t any = 0;
    for (unsigned i = 0; i < 32; ++i) any |= hash[i];
    return any != 0;
}
bool sh_input_tx_bind(struct sh_input_transaction *t, const uint8_t apply_hash[32], const uint8_t rollback_hash[32])
{
    if (!t || t->phase != SH_INPUT_TX_PREPARED || t->bound ||
        !tx_hash_valid(apply_hash) || !tx_hash_valid(rollback_hash) || !memcmp(apply_hash, rollback_hash, 32)) return false;
    memcpy(t->apply_hash, apply_hash, 32); memcpy(t->rollback_hash, rollback_hash, 32);
    t->bound = true;
    return true;
}
bool sh_input_tx_queue(struct sh_input_transaction *t, uint64_t now)
{
    if (!t || t->phase != SH_INPUT_TX_PREPARED || !t->bound || now > UINT64_MAX - SH_INPUT_TX_TIMEOUT) return false;
    t->phase = SH_INPUT_TX_APPLYING;
    t->working_known = false;
    t->deadline = now + SH_INPUT_TX_TIMEOUT;
    return true;
}
static uint32_t tx_expected_revision(const struct sh_input_transaction *t)
{
    if (t->phase == SH_INPUT_TX_APPLYING) return t->apply_revision;
    if (t->phase == SH_INPUT_TX_ROLLING_BACK) return t->rollback_revision;
    return 0;
}
static void tx_confirm(struct sh_input_transaction *t)
{
    if (!t->completed || !t->activation_requested) return;
    bool rollback = t->phase == SH_INPUT_TX_ROLLING_BACK;
    t->working.appid = t->appid; t->working.epoch = t->epoch; t->working.sequence = t->sequence;
    t->working.revision = rollback ? t->rollback_revision : t->apply_revision;
    memcpy(t->working.hash, rollback ? t->rollback_hash : t->apply_hash, 32);
    t->working_known = true; t->deadline = 0;
    t->phase = rollback ? SH_INPUT_TX_ROLLED_BACK : SH_INPUT_TX_CONFIRMED;
}
bool sh_input_tx_activated(struct sh_input_transaction *t, uint32_t revision, uint64_t now)
{
    if (!t || !t->deadline || now >= t->deadline || !revision || revision != tx_expected_revision(t)) return false;
    t->activation_requested = true;
    tx_confirm(t);
    return true;
}
bool sh_input_tx_completion(struct sh_input_transaction *t, const struct sh_input_load_result *result, uint64_t now)
{
    if (!t || !t->deadline || now >= t->deadline || !result || !result->completed || result->appid != t->appid || result->controller ||
        !result->revision || result->revision != tx_expected_revision(t)) return false;
    t->completed = true;
    tx_confirm(t);
    return true;
}
bool sh_input_tx_fail(struct sh_input_transaction *t, uint64_t now)
{
    if (!t) return false;
    if (t->phase == SH_INPUT_TX_PREPARED) {
        t->phase = SH_INPUT_TX_CANCELLED; /* reserved revisions stay consumed */
        return false;
    }
    if (t->phase != SH_INPUT_TX_APPLYING && t->phase != SH_INPUT_TX_ROLLING_BACK) return false;
    t->working_known = false;
    if (t->phase == SH_INPUT_TX_ROLLING_BACK || now > UINT64_MAX - SH_INPUT_TX_TIMEOUT) {
        t->phase = SH_INPUT_TX_FAILED; t->deadline = 0;
        return false;
    }
    t->phase = SH_INPUT_TX_ROLLING_BACK;
    t->completed = t->activation_requested = false;
    t->deadline = now + SH_INPUT_TX_TIMEOUT;
    return true;
}
bool sh_input_tx_poll(struct sh_input_transaction *t, uint64_t now)
{
    if (!t || !t->deadline || now < t->deadline) return false;
    return sh_input_tx_fail(t, now);
}
bool sh_input_tx_disconnect(struct sh_input_transaction *t, uint32_t new_epoch)
{
    if (!t || !new_epoch || new_epoch <= t->epoch) return false;
    t->epoch = new_epoch; t->phase = SH_INPUT_TX_CANCELLED;
    t->deadline = 0; t->working_known = false;
    t->completed = t->activation_requested = false;
    return true;
}

bool sh_input_apply_parse(const uint8_t *data, size_t size, struct sh_input_apply *out)
{
    if (!data || !out || size < SH_INPUT_APPLY_HEADER || size > SH_INPUT_APPLY_LIMIT ||
        memcmp(data, "MSITXN02", 8)) return false;
    struct sh_input_apply value = {0};
    memcpy(value.scope.session, data + 8, 16);
    value.scope.appid = get32(data + 24); value.scope.controller = get32(data + 28);
    value.scope.epoch = get32(data + 32); value.scope.sequence = get32(data + 36);
    value.scope.action = SH_INPUT_APPLY;
    value.apply_revision = get32(data + 40); value.rollback_revision = get32(data + 44);
    memcpy(value.apply_hash, data + 48, 32); memcpy(value.rollback_hash, data + 80, 32);
    value.apply_size = get32(data + 112); value.rollback_size = get32(data + 116);
    uint8_t any = 0;
    for (unsigned i = 0; i < 16; ++i) any |= value.scope.session[i];
    if (!any || !value.scope.appid || value.scope.controller || !value.scope.epoch || !value.scope.sequence ||
        !value.apply_revision || value.apply_revision >= INT32_MAX || value.rollback_revision != value.apply_revision + 1 ||
        !value.apply_size || value.apply_size >= SH_INPUT_LAYOUT_LIMIT || !value.rollback_size ||
        value.rollback_size >= SH_INPUT_LAYOUT_LIMIT ||
        value.apply_size + value.rollback_size != size - SH_INPUT_APPLY_HEADER ||
        !tx_hash_valid(value.apply_hash) || !tx_hash_valid(value.rollback_hash) ||
        !memcmp(value.apply_hash, value.rollback_hash, 32)) return false;
    value.apply = (const char *)data + SH_INPUT_APPLY_HEADER;
    value.rollback = value.apply + value.apply_size;
    uint32_t revision = 0;
    if (!sh_input_vdf_revision(value.apply, value.apply_size, &revision) || revision != value.apply_revision ||
        !sh_input_vdf_revision(value.rollback, value.rollback_size, &revision) || revision != value.rollback_revision) return false;
    *out = value;
    return true;
}
bool sh_input_apply_scope(const struct sh_input_apply *apply, const struct sh_input_command *command)
{
    return apply && command && command->action == SH_INPUT_APPLY &&
        !memcmp(apply->scope.session, command->session, 16) && apply->scope.appid == command->appid &&
        apply->scope.controller == command->controller && apply->scope.epoch == command->epoch &&
        apply->scope.sequence == command->sequence;
}
size_t sh_input_tx_feed(const struct sh_input_service *service, const struct sh_input_transaction *t, uint8_t *out, size_t cap)
{
    if (!service || !t || !out || !service->enabled || cap < SH_INPUT_TX_FEED_SIZE ||
        !t->appid || t->appid != service->appid || !t->epoch || t->epoch != service->epoch) return 0;
    memcpy(out, "MSITXFD2", 8); memcpy(out + 8, service->session, 16);
    put32(out + 24, t->appid); put32(out + 28, 0); put32(out + 32, t->epoch);
    put32(out + 36, t->sequence); put32(out + 40, (uint32_t)t->phase);
    put32(out + 44, t->high_water); put32(out + 48, t->apply_revision); put32(out + 52, t->rollback_revision);
    put32(out + 56, t->working.revision); memcpy(out + 60, t->working.hash, 32);
    put32(out + 92, t->working_known ? 1 : 0);
    return SH_INPUT_TX_FEED_SIZE;
}

size_t sh_input_baseline_feed(const struct sh_input_service *s, uint32_t revision,
    const char *data, size_t size, uint8_t *out, size_t cap)
{
    uint32_t parsed = 0;
    if (!s || !s->enabled || !s->connected || !s->appid || !s->epoch || !out ||
        !size || size >= SH_INPUT_LAYOUT_LIMIT || cap < SH_INPUT_BASELINE_HEADER + size ||
        !sh_input_vdf_revision(data, size, &parsed) || parsed != revision) return 0;
    memcpy(out, "MSIBAS02", 8); memcpy(out + 8, s->session, 16);
    put32(out + 24, s->appid); put32(out + 28, 0); put32(out + 32, s->epoch);
    put32(out + 36, revision); put32(out + 40, (uint32_t)size);
    memcpy(out + SH_INPUT_BASELINE_HEADER, data, size);
    return SH_INPUT_BASELINE_HEADER + size;
}
bool sh_input_baseline_parse(const uint8_t *data, size_t size, struct sh_input_baseline *out)
{
    if (!data || !out || size <= SH_INPUT_BASELINE_HEADER ||
        size >= SH_INPUT_BASELINE_HEADER + SH_INPUT_LAYOUT_LIMIT || memcmp(data, "MSIBAS02", 8)) return false;
    struct sh_input_baseline value = {0};
    memcpy(value.session, data + 8, 16); value.appid = get32(data + 24); value.epoch = get32(data + 32);
    value.revision = get32(data + 36); value.size = get32(data + 40);
    uint8_t any = 0; for (unsigned i=0; i<16; ++i) any |= value.session[i];
    if (!any || !value.appid || get32(data + 28) || !value.epoch || value.revision > INT32_MAX ||
        value.size != size - SH_INPUT_BASELINE_HEADER) return false;
    value.data = (const char *)data + SH_INPUT_BASELINE_HEADER;
    uint32_t revision = 0;
    if (!sh_input_vdf_revision(value.data, value.size, &revision) || revision != value.revision) return false;
    *out = value; return true;
}
