/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md */
#include "steam_input_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
int main(void)
{
    uint8_t callback[41] = {0};
    put32(callback, 480); callback[8] = 7; callback[16] = 0xff;
    put32(callback + 24, 2); put32(callback + 28, 3); callback[33] = 1;
    struct sh_input_configuration configuration = {0};
    for (size_t i = 0; i < 40; ++i) assert(!sh_input_configuration_parse(callback, i, &configuration));
    assert(!sh_input_configuration_parse(callback, 41, &configuration));
    assert(sh_input_configuration_parse(callback, 40, &configuration));
    assert(configuration.appid == 480 && configuration.device == 7 && configuration.major == 2 &&
           configuration.minor == 3 && !configuration.native_actions && configuration.gamepad_output);
    callback[32] = 2; assert(!sh_input_configuration_parse(callback, 40, &configuration));
    callback[32] = 0; callback[8] = 0;
    assert(!sh_input_configuration_parse(callback, 40, &configuration));
    uint8_t load[33] = {0}; load[0] = 1;
    put32(load + 4, 0); put32(load + 8, 480); put32(load + 24, 731);
    memset(load + 16, 0xff, 8); /* creator must not affect metadata parsing */
    struct sh_input_load_result result = {0};
    for (size_t size = 0; size < 32; ++size) assert(!sh_input_load_result_parse(load, size, &result));
    assert(!sh_input_load_result_parse(load, 33, &result));
    assert(sh_input_load_result_parse(load, 32, &result));
    assert(result.completed && result.controller == 0 && result.appid == 480 && result.revision == 731);
    load[0] = 2; assert(!sh_input_load_result_parse(load, 32, &result));
    assert(result.completed && result.revision == 731); /* malformed input leaves output unchanged */
    load[0] = 0; assert(sh_input_load_result_parse(load, 32, &result) && !result.completed);
    put32(load + 4, 16); assert(!sh_input_load_result_parse(load, 32, &result));
    put32(load + 4, 0); put32(load + 8, 0); assert(!sh_input_load_result_parse(load, 32, &result));
    uint32_t revision = 99;
    const char *documents[] = {
        "controller_mappings { revision 731 unknown { revision 123 } unknown value }",
        "\"controller_mappings\" { // revision 1\n \"revision\" \"731\" }"
    };
    for (unsigned i = 0; i < sizeof documents / sizeof *documents; ++i) {
        assert(sh_input_vdf_revision(documents[i], strlen(documents[i]), &revision) && revision == 731);
    }
    const char *empty = "controller_mappings { nested { revision 731 } }";
    assert(sh_input_vdf_revision(empty, strlen(empty), &revision) && revision == 0);
    const char *invalid[] = {
        "controller_mappings { Revision 1 }", "controller_mappings { revision 1 REVISION 2 }",
        "controller_mappings { revision 1 revision 2 }", "controller_mappings { revision {} }",
        "controller_mappings { revision 1x }", "controller_mappings { revision -1 }",
        "controller_mappings { revision 2147483648 }", "controller_mappings { revision 1",
        "controller_mappings { revision 0000000000000000000000000000000000001 }",
        "other { revision 1 }", "controller_mappings {} controller_mappings {}",
        "controller_mappings { key \"unterminated }", "controller_mappings { key }"
    };
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        revision = 99;
        assert(!sh_input_vdf_revision(invalid[i], strlen(invalid[i]), &revision) && revision == 99);
    }
    assert(!sh_input_vdf_revision(empty, 1u << 20, &revision));
    char deep[1024] = "controller_mappings { ";
    for (unsigned i = 0; i < 65; ++i) strcat(deep, "n { ");
    for (unsigned i = 0; i < 66; ++i) strcat(deep, "} ");
    assert(!sh_input_vdf_revision(deep, strlen(deep), &revision));
    char *many = malloc(500000); assert(many); strcpy(many, "controller_mappings {");
    size_t at = strlen(many);
    for (unsigned i = 0; i < 50000; ++i) { memcpy(many + at, " k v", 4); at += 4; }
    many[at++] = '}'; many[at] = 0;
    assert(!sh_input_vdf_revision(many, at, &revision)); free(many);
    /* Preserve every non-metadata byte, including unknown/nested bindings,
     * comments and escaped values. Do not round-trip the document as a map. */
    const char *raw = "// revision 3\n\"controller_mappings\" {\n \"revision\" /*bad*/ }";
    char prepared[512]; memset(prepared, '!', sizeof prepared); size_t written = 999;
    assert(!sh_input_vdf_materialize(raw, strlen(raw), 732, prepared, sizeof prepared, &written));
    assert(prepared[0] == '!' && written == 999);
    raw = "// revision 3\n\"controller_mappings\" {\n \"revision\" 0007 //keep\n unknown { revision 9 binding \"a\\\"b\" }\n}";
    const char *expected = "// revision 3\n\"controller_mappings\" {\n \"revision\" \"732\" //keep\n unknown { revision 9 binding \"a\\\"b\" }\n}";
    assert(sh_input_vdf_materialize(raw, strlen(raw), 732, prepared, sizeof prepared, &written));
    assert(written == strlen(expected) && !strcmp(prepared, expected));
    assert(sh_input_vdf_revision(prepared, written, &revision) && revision == 732);
    raw = "controller_mappings {//keep\n nested { revision 8 } }";
    expected = "controller_mappings {\n\"revision\" \"2147483647\"\n//keep\n nested { revision 8 } }";
    assert(sh_input_vdf_materialize(raw, strlen(raw), INT32_MAX, prepared, sizeof prepared, &written));
    assert(!strcmp(prepared, expected));
    memset(prepared, '!', sizeof prepared); written = 999;
    assert(!sh_input_vdf_materialize(raw, strlen(raw), 1, prepared, 2, &written));
    assert(!sh_input_vdf_materialize(raw, strlen(raw), 0, prepared, sizeof prepared, &written));
    assert(!sh_input_vdf_materialize(raw, strlen(raw), UINT32_MAX, prepared, sizeof prepared, &written));
    assert(prepared[0] == '!' && written == 999);
    strcpy(prepared, raw);
    assert(!sh_input_vdf_materialize(prepared, strlen(prepared), 1, prepared, sizeof prepared, &written));
    assert(!strcmp(prepared, raw));
    assert(!sh_input_vdf_materialize(prepared, strlen(prepared), 1, prepared + 2, sizeof prepared - 2, &written));
    assert(!strcmp(prepared, raw));
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i)
        assert(!sh_input_vdf_materialize(invalid[i], strlen(invalid[i]), 1, prepared, sizeof prepared, &written));
    puts("PASS: revision queue copies preserve unknown bindings, insert metadata, reject ambiguity/overlap and fail without output mutation");
    struct sh_input_service s = {0};
    assert(!sh_input_session_parse(NULL, s.session));
    assert(!sh_input_session_parse("00000000000000000000000000000000", s.session));
    assert(!sh_input_session_parse("0123456789ABCDEF0123456789ABCDEF", s.session));
    assert(sh_input_session_parse("0123456789abcdef0123456789abcdef", s.session));
    s.enabled = s.available = true; s.appid = 480;
    sh_input_service_connection(&s, true); assert(s.epoch == 1);
    uint8_t wire[SH_INPUT_COMMAND_SIZE + 1] = {0};
    memcpy(wire, "MSICMD01", 8); memcpy(wire + 8, s.session, 16);
    put32(wire + 24, 480); put32(wire + 32, 1); put32(wire + 36, 1); put32(wire + 40, SH_INPUT_QUERY);
    struct sh_input_command c;
    for (size_t i = 0; i < SH_INPUT_COMMAND_SIZE; ++i) assert(!sh_input_command_parse(wire, i, &c));
    assert(!sh_input_command_parse(wire, sizeof wire, &c));
    assert(sh_input_command_parse(wire, SH_INPUT_COMMAND_SIZE, &c));
    struct sh_input_command bad = c;
    bad.session[0] ^= 1; assert(sh_input_service_accept(&s, &bad) == SH_INPUT_STALE);
    bad = c; bad.appid++; assert(sh_input_service_accept(&s, &bad) == SH_INPUT_STALE);
    bad = c; bad.controller++; assert(sh_input_service_accept(&s, &bad) == SH_INPUT_STALE);
    bad = c; bad.epoch++; assert(sh_input_service_accept(&s, &bad) == SH_INPUT_STALE);
    assert(s.last_sequence == 0);
    assert(sh_input_service_accept(&s, &c) == SH_INPUT_OK);
    assert(sh_input_service_accept(&s, &c) == SH_INPUT_STALE);
    strcpy(s.selection, "AC-480-controller_ps5"); s.workshop = UINT64_C(0xfedcba9876543210);
    uint8_t feed[SH_INPUT_FEED_CAP + 1]; memset(feed, 0xcc, sizeof feed);
    s.state = SH_INPUT_ACTIVATION_REQUESTED;
    size_t length = sh_input_service_feed(&s, feed, sizeof feed - 1);
    assert(length == SH_INPUT_FEED_HEADER + strlen(s.selection));
    assert(!memcmp(feed, "MSIFEED1", 8) && !memcmp(feed + 8, s.session, 16));
    assert(feed[24] == 0xe0 && feed[25] == 1 && feed[32] == 1 && feed[36] == 1);
    assert(feed[44] == SH_INPUT_ACTIVATION_REQUESTED && feed[48] == 1 && feed[52] == 3);
    assert(feed[56] == 0x10 && feed[63] == 0xfe && feed[64] == SH_INPUT_FORWARDED);
    assert(!memcmp(feed + SH_INPUT_FEED_HEADER, s.selection, strlen(s.selection)) && feed[length] == 0xcc);
    assert(!sh_input_service_feed(&s, feed, length - 1));
    for (uint32_t action = SH_INPUT_APPLY; action <= SH_INPUT_REVISION_PROBE; ++action) {
        c.sequence++; c.action = action;
        assert(sh_input_service_accept(&s, &c) == SH_INPUT_ACK_UNAVAILABLE);
        assert(!strcmp(s.selection, "AC-480-controller_ps5") && s.state == SH_INPUT_ACTIVATION_REQUESTED);
    }
    sh_input_service_connection(&s, false); assert(s.epoch == 2 && !s.selection[0] && !s.workshop);
    sh_input_service_connection(&s, true); assert(s.epoch == 3);
    c.sequence++; c.action = SH_INPUT_QUERY;
    assert(sh_input_service_accept(&s, &c) == SH_INPUT_STALE); /* before reconnect */
    c.epoch = 3; assert(sh_input_service_accept(&s, &c) == SH_INPUT_OK);
    c.sequence = 1; assert(sh_input_service_accept(&s, &c) == SH_INPUT_STALE);
    c.sequence = UINT32_MAX; c.action = SH_INPUT_SOURCES; s.available = false;
    assert(sh_input_service_accept(&s, &c) == SH_INPUT_UNAVAILABLE);
    c.sequence = 1; assert(sh_input_service_accept(&s, &c) == SH_INPUT_STALE); /* no wrap */
    s.epoch = UINT32_MAX; sh_input_service_connection(&s, false); assert(!s.enabled);
    assert(!sh_input_service_feed(&s, feed, sizeof feed));
    memset(s.selection, 'x', sizeof s.selection); s.enabled = true;
    assert(!sh_input_service_feed(&s, feed, sizeof feed));
    puts("PASS: Steam Input protocol bounds, scope/lease/sequence rejection, explicit no-ACK result and nonmutating queries");
}
