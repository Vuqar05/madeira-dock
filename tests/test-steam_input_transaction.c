/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md */
#include "steam_input_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint8_t a[32] = {1}, b[32] = {2}, c[32] = {3}, zero[32] = {0};
static void unchanged(struct sh_input_transaction *t, struct sh_input_transaction before)
{ assert(!memcmp(t, &before, sizeof before)); }
static struct sh_input_load_result completion(uint32_t revision)
{ return (struct sh_input_load_result){.appid=480, .controller=0, .revision=revision, .completed=true}; }
int main(void)
{
    struct sh_input_transaction t = {0}, before = t;
    assert(!sh_input_tx_init(&t, 0, 1, 0)); unchanged(&t, before);
    assert(!sh_input_tx_init(&t, 480, 0, 0)); unchanged(&t, before);
    assert(!sh_input_tx_init(&t, 480, 1, UINT32_MAX)); unchanged(&t, before);
    assert(sh_input_tx_init(&t, 480, 1, 731));
    assert(sh_input_tx_reserve(&t, 1, 1, 7));
    assert(t.apply_revision == 732 && t.rollback_revision == 733 && t.high_water == 733);
    before = t;
    assert(!sh_input_tx_reserve(&t, 1, 2, 7)); unchanged(&t, before);
    assert(!sh_input_tx_queue(&t, 0)); unchanged(&t, before);
    assert(!sh_input_tx_bind(&t, zero, b)); unchanged(&t, before);
    assert(!sh_input_tx_bind(&t, a, a)); unchanged(&t, before);
    assert(sh_input_tx_bind(&t, a, b)); before = t;
    assert(!sh_input_tx_bind(&t, c, b)); unchanged(&t, before);
    assert(!sh_input_tx_queue(&t, UINT64_MAX)); unchanged(&t, before);
    assert(sh_input_tx_queue(&t, 100));
    struct sh_input_load_result r = completion(732);
    r.appid++; assert(!sh_input_tx_completion(&t, &r, 101)); r.appid--;
    r.controller++; assert(!sh_input_tx_completion(&t, &r, 101)); r.controller--;
    r.completed = false; assert(!sh_input_tx_completion(&t, &r, 101)); r.completed = true;
    r.revision--; assert(!sh_input_tx_completion(&t, &r, 101)); r.revision++;
    assert(sh_input_tx_completion(&t, &r, 101));
    assert(!t.working_known && t.phase == SH_INPUT_TX_APPLYING);
    assert(!sh_input_tx_activated(&t, 731, 101));
    assert(sh_input_tx_activated(&t, 732, 102));
    assert(t.phase == SH_INPUT_TX_CONFIRMED && t.working_known && !t.deadline);
    assert(t.working.appid == 480 && t.working.epoch == 1 && t.working.sequence == 1 &&
           t.working.revision == 732 && !memcmp(t.working.hash, a, 32));
    assert(!sh_input_tx_completion(&t, &r, 103));
    before = t; assert(!sh_input_tx_reserve(&t, 1, 1, 0)); unchanged(&t, before);
    assert(!sh_input_tx_reserve(&t, 2, 2, 0)); unchanged(&t, before);
    assert(sh_input_tx_reserve(&t, 1, 2, 0));
    assert(t.apply_revision == 734 && t.rollback_revision == 735 && t.working_known);
    assert(sh_input_tx_bind(&t, c, a) && sh_input_tx_queue(&t, 200));
    assert(!t.working_known && !memcmp(t.working.hash, a, 32));
    assert(sh_input_tx_activated(&t, 734, 201));
    r = completion(732); assert(!sh_input_tx_completion(&t, &r, 202));
    assert(!sh_input_tx_poll(&t, 15199));
    r = completion(734); assert(!sh_input_tx_completion(&t, &r, 15200)); /* exact timeout */
    assert(sh_input_tx_poll(&t, 15200));
    assert(t.phase == SH_INPUT_TX_ROLLING_BACK && !t.completed && !t.activation_requested);
    assert(!sh_input_tx_poll(&t, 15201)); /* rollback is submitted only once */
    r = completion(734); assert(!sh_input_tx_completion(&t, &r, 15201));
    r = completion(732); assert(!sh_input_tx_completion(&t, &r, 15201));
    r = completion(735); assert(sh_input_tx_completion(&t, &r, 15201));
    assert(!t.working_known);
    assert(!sh_input_tx_activated(&t, 734, 15202));
    assert(sh_input_tx_activated(&t, 735, 15202));
    assert(t.phase == SH_INPUT_TX_ROLLED_BACK && t.working_known && t.working.revision == 735 &&
           t.working.sequence == 2 && !memcmp(t.working.hash, a, 32));
    /* Explicit failure initiates the same bounded rollback. A failed rollback
     * retains history but cannot be called active or accept further work. */
    assert(sh_input_tx_reserve(&t, 1, 3, 1000));
    assert(t.apply_revision == 1001 && t.rollback_revision == 1002);
    assert(sh_input_tx_bind(&t, c, a) && sh_input_tx_queue(&t, 16000));
    assert(sh_input_tx_fail(&t, 16001));
    assert(!sh_input_tx_poll(&t, 31001) && t.phase == SH_INPUT_TX_FAILED && !t.working_known);
    assert(!memcmp(t.working.hash, a, 32));
    r = completion(1002); assert(!sh_input_tx_completion(&t, &r, 31002));
    before = t; assert(!sh_input_tx_reserve(&t, 1, 4, 0)); unchanged(&t, before);
    assert(sh_input_tx_disconnect(&t, 2) && t.high_water == 1002 && t.sequence == 3);
    assert(!sh_input_tx_disconnect(&t, 2));
    assert(sh_input_tx_reserve(&t, 2, 4, 0) && t.apply_revision == 1003);
    before = t; assert(!sh_input_tx_reserve(&t, 1, 5, 0)); unchanged(&t, before);
    assert(!sh_input_tx_fail(&t, 0) && t.phase == SH_INPUT_TX_CANCELLED);
    assert(t.high_water == 1004); /* cancellation never recycles a revision */
    assert(sh_input_tx_reserve(&t, 2, 5, 0) && t.apply_revision == 1005);
    assert(sh_input_tx_bind(&t, a, b) && sh_input_tx_queue(&t, 32000));
    assert(sh_input_tx_disconnect(&t, 3));
    r = completion(1005); assert(!sh_input_tx_completion(&t, &r, 32001));
    assert(sh_input_tx_reserve(&t, 3, 6, 0) && t.apply_revision == 1007);
    assert(sh_input_tx_bind(&t, a, b) && sh_input_tx_queue(&t, 33000));
    r = completion(1005); assert(!sh_input_tx_completion(&t, &r, 33001));
    assert(sh_input_tx_activated(&t, 1007, 33002));
    r = completion(1007); assert(sh_input_tx_completion(&t, &r, 33003) && t.working_known);
    before = t;
    assert(!sh_input_tx_reserve(&t, 3, 7, INT32_MAX - 1)); unchanged(&t, before);
    assert(sh_input_tx_reserve(&t, 3, 7, INT32_MAX - 2));
    assert(t.high_water == INT32_MAX);
    sh_input_tx_fail(&t, 0); before = t;
    assert(!sh_input_tx_reserve(&t, 3, 8, 0)); unchanged(&t, before);
    puts("PASS: transaction reservation/content binding, two-part confirmation, serialization, stale/late callbacks, bounded rollback, history retention, reconnect and exhaustion");
}
