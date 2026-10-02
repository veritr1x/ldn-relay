/* Diagnostic independent generators, explicitly enabled by test build only. */
- (void)duplexTick {
    if(!duplexActive)return;
    uint64_t elapsed=clockMs()-duplexStart;
    unsigned target=(unsigned)(MIN(elapsed,60000)*duplexRate/1000);
    while(duplexGenerated<target){
        unsigned seq=duplexGenerated++;uint8_t p[RELAY_MAX_UDP+10];size_t n=duplexOutSize?duplexOutSize:game_size(1,seq);
        memset(p,(uint8_t)(seq%251),n);p[0]=0x71;p[1]=1;p[2]=0;lr_put32(p+3,seq);lr_put64(p+7,clockMs());
        if(outgoing.count>=32){duplexDropped++;continue;}
        [outgoing addObject:[NSData dataWithBytes:p length:n]];duplexBytes+=n;
    }
    if(elapsed>=65000 && !duplexReported && outgoing.count<32){
        uint8_t p[39]={0x72};lr_put32(p+3,duplexGenerated);lr_put32(p+7,duplexDropped);lr_put32(p+11,(uint32_t)duplexSeen.count);
        lr_put32(p+15,duplexCorrupt);lr_put32(p+19,duplexDuplicates);lr_put64(p+23,duplexBytes);lr_put64(p+31,duplexRxBytes);
        [outgoing addObject:[NSData dataWithBytes:p length:sizeof(p)]];duplexReported=YES;duplexActive=NO;
    }
}
- (BOOL)duplexReceive:(NSData *)data {
    const uint8_t *p=data.bytes;size_t n=data.length;
    if(n==15 && p[0]==0x73){if(outgoing.count>=32){blockedReplySlots=1;return NO;}[outgoing addObject:data];return YES;}
    if(n==11 && p[0]==0x70){
        unsigned rate=lr_get32(p+3),outSize=lr_get32(p+7);if(!rate || rate>200 || (outSize && (outSize<15 || outSize>RELAY_MAX_UDP+10)))return YES;
        if(outgoing.count>=32){blockedReplySlots=1;return NO;}
        duplexOutSize=outSize;duplexRate=rate;duplexStart=clockMs();duplexGenerated=duplexDropped=duplexCorrupt=duplexDuplicates=0;duplexBytes=duplexRxBytes=0;duplexSeen=[NSMutableIndexSet new];duplexActive=YES;duplexReported=NO;
        uint8_t ack[3]={0x74};[outgoing addObject:[NSData dataWithBytes:ack length:3]];return YES;
    }
    if(n>=15 && p[0]==0x71){
        unsigned seq=lr_get32(p+3);BOOL valid=p[1]==0 && p[2]==0 && seq<duplexRate*60 && n==game_size(0,seq);
        for(size_t i=15;i<n && valid;i++)if(p[i]!=(uint8_t)(seq%251))valid=NO;
        if(!valid){duplexCorrupt++;return YES;}
        if([duplexSeen containsIndex:seq]){duplexDuplicates++;return YES;}
        [duplexSeen addIndex:seq];duplexRxBytes+=n;return YES;
    }
    return YES;
}
