- (void)onewayTick {
 if(!oneway.active || !onewaySend)return;
 uint64_t elapsed=clockMs()-oneway.start,duration=oneway.seconds*1000u;
 unsigned target=(unsigned)(MIN(elapsed,duration)*oneway.rate/1000);
 while(oneway.next<target){uint8_t p[OW_SIZE];ow_packet(p,oneway.id,oneway.next++);if(outgoing.count<32){[outgoing addObject:[NSData dataWithBytes:p length:sizeof(p)]];oneway.accepted++;}else oneway.dropped++;}
 if(elapsed>=duration && outgoing.count<32){uint8_t end[15]={OW_END};lr_put16(end+1,oneway.id);lr_put32(end+3,oneway.next);lr_put32(end+7,oneway.accepted);lr_put32(end+11,oneway.dropped);[outgoing addObject:[NSData dataWithBytes:end length:sizeof(end)]];oneway.active=false;}
}
