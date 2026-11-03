/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md */
#include "steam_input_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put(uint8_t *p, uint32_t v) { for (unsigned i=0; i<4; ++i) p[i]=(uint8_t)(v>>(8*i)); }
int main(void)
{
    const char *a="controller_mappings { revision 732 }", *b="controller_mappings { revision 733 }";
    size_t al=strlen(a), bl=strlen(b), size=SH_INPUT_APPLY_HEADER+al+bl;
    uint8_t bytes[512]={0}; memcpy(bytes,"MSITXN02",8); bytes[8]=1;
    put(bytes+24,480); put(bytes+32,1); put(bytes+36,2); put(bytes+40,732); put(bytes+44,733);
    bytes[48]=1; bytes[80]=2; put(bytes+112,(uint32_t)al); put(bytes+116,(uint32_t)bl);
    memcpy(bytes+120,a,al); memcpy(bytes+120+al,b,bl);
    struct sh_input_apply parsed={0};
    for(size_t i=0;i<size;++i) assert(!sh_input_apply_parse(bytes,i,&parsed));
    assert(!sh_input_apply_parse(bytes,size+1,&parsed));
    assert(sh_input_apply_parse(bytes,size,&parsed));
    assert(parsed.apply_size==al && parsed.rollback_size==bl && !memcmp(parsed.apply,a,al));
    struct sh_input_command command=parsed.scope;
    assert(sh_input_apply_scope(&parsed,&command));
    command.sequence++; assert(!sh_input_apply_scope(&parsed,&command)); command=parsed.scope;
    command.epoch++; assert(!sh_input_apply_scope(&parsed,&command)); command=parsed.scope;
    command.session[0]++; assert(!sh_input_apply_scope(&parsed,&command)); command=parsed.scope;
    command.appid++; assert(!sh_input_apply_scope(&parsed,&command)); command=parsed.scope;
    command.controller++; assert(!sh_input_apply_scope(&parsed,&command)); command=parsed.scope;
    command.action=SH_INPUT_QUERY; assert(!sh_input_apply_scope(&parsed,&command));
    const unsigned invalid_offsets[] = {28,40,44};
    for(unsigned i=0;i<sizeof invalid_offsets/sizeof *invalid_offsets;++i) {
        unsigned offset=invalid_offsets[i]; uint8_t saved[4]; memcpy(saved,bytes+offset,4); memset(bytes+offset,0xff,4);
        assert(!sh_input_apply_parse(bytes,size,&parsed)); memcpy(bytes+offset,saved,4);
    }
    const unsigned required_offsets[] = {24,32,36};
    for(unsigned i=0;i<sizeof required_offsets/sizeof *required_offsets;++i) {
        unsigned offset=required_offsets[i]; uint8_t saved[4]; memcpy(saved,bytes+offset,4); memset(bytes+offset,0,4);
        assert(!sh_input_apply_parse(bytes,size,&parsed)); memcpy(bytes+offset,saved,4);
    }
    put(bytes+112,UINT32_MAX); assert(!sh_input_apply_parse(bytes,size,&parsed)); put(bytes+112,(uint32_t)al);
    bytes[120+al-1]=0; assert(!sh_input_apply_parse(bytes,size,&parsed)); bytes[120+al-1]='}';
    bytes[48]=0; assert(!sh_input_apply_parse(bytes,size,&parsed)); bytes[48]=1;
    put(bytes+44,734); assert(!sh_input_apply_parse(bytes,size,&parsed)); put(bytes+44,733);
    struct sh_input_service s={0}; s.enabled=true; s.appid=480; s.epoch=1; s.session[0]=1;
    struct sh_input_transaction t={0}; assert(sh_input_tx_init(&t,480,1,731));
    assert(sh_input_tx_reserve(&t,1,2,731));
    uint8_t hashA[32]={1},hashB[32]={2}; assert(sh_input_tx_bind(&t,hashA,hashB));
    assert(sh_input_tx_queue(&t,100));
    uint8_t feed[97]; memset(feed,0xee,sizeof feed);
    assert(!sh_input_tx_feed(&s,&t,feed,95));
    assert(sh_input_tx_feed(&s,&t,feed,96)==96 && feed[96]==0xee);
    assert(!memcmp(feed,"MSITXFD2",8) && feed[40]==SH_INPUT_TX_APPLYING && feed[92]==0);
    struct sh_input_load_result result={.appid=480,.revision=732,.completed=true};
    assert(sh_input_tx_completion(&t,&result,101) && sh_input_tx_activated(&t,732,102));
    assert(sh_input_tx_feed(&s,&t,feed,96)==96 && feed[92]==1 && feed[60]==1);
    s.epoch++; assert(!sh_input_tx_feed(&s,&t,feed,96));
    s.epoch = 1; s.connected = true;
    size_t count = sh_input_baseline_feed(&s, 732, a, al, bytes, sizeof bytes);
    assert(count == SH_INPUT_BASELINE_HEADER + al);
    struct sh_input_baseline baseline;
    for(size_t i=0;i<count;++i) assert(!sh_input_baseline_parse(bytes,i,&baseline));
    assert(sh_input_baseline_parse(bytes,count,&baseline) && baseline.appid==480 && baseline.epoch==1 && baseline.revision==732);
    assert(baseline.size==al && !memcmp(baseline.data,a,al));
    bytes[36]++; assert(!sh_input_baseline_parse(bytes,count,&baseline)); bytes[36]--;
    assert(!sh_input_baseline_feed(&s, 731, a, al, bytes, sizeof bytes));
    puts("PASS: bounded local apply envelopes, metadata/scope/length rejection and exact hash-bound transaction feed");
}
