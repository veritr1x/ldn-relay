static LrOneWay oneway,oneway_rx;
static bool oneway_send;
static BtdrvAddress oneway_peer;
static void oneway_command(const uint8_t *p,size_t n){
 if(p[0]==OW_START){
  if(oneway.active || oneway_rx.active || bench.active || !ow_start(&oneway,p,n,now_ms()))return;
  oneway_send=p[3]!=1;
  oneway_rx.active=false;
  if(p[3]==2){uint8_t start[8];memcpy(start,p,8);lr_put16(start+1,oneway.id|0x8000);ow_start(&oneway_rx,start,8,now_ms());}
  else if(!oneway_send){oneway_rx=oneway;oneway.active=false;}
  note("ONEWAY transport configured_frame=%u default_window=3 tuning=%u",codec.frame_limit,atomic_load(&stream_tuning));
  note("ONEWAY START id=%u direction=%s rate=%u seconds=%u",oneway.id,oneway_send?"Switch-to-phone":"phone-to-Switch",oneway.rate,oneway.seconds);
  if(oneway_send && !enqueue(p,n)){oneway.active=false;note("ONEWAY start enqueue failed");}
 }else if(p[0]==OW_DATA){ow_receive(&oneway_rx,p,n);
 }else if(p[0]==OW_END){uint8_t out[31];if(ow_report(&oneway_rx,p,n,now_ms(),out)){enqueue(out,sizeof(out));note("ONEWAY RX received=%u corrupt=%u duplicates=%u elapsed_ms=%u",oneway_rx.received,oneway_rx.corrupt,oneway_rx.duplicates,lr_get32(out+27));}
 }else if(p[0]==OW_REPORT && n==31){note("ONEWAY REPORT id=%u offered=%u accepted=%u dropped=%u received=%u corrupt=%u duplicates=%u elapsed_ms=%u",lr_get16(p+1),lr_get32(p+3),lr_get32(p+7),lr_get32(p+11),lr_get32(p+15),lr_get32(p+19),lr_get32(p+23),lr_get32(p+27));
 }else if(p[0]==OW_PARAM && n==5){
  unsigned interval=lr_get16(p+3);Service service={0};Result rc=smGetService(&service,"btdrv");result("Connection tuning: open btdrv",rc);
  if(R_SUCCEEDED(rc)){
   if(interval && (interval==6 || interval==9 || interval==12 || interval==24) && hosversionAtLeast(9,0,0)){
    /* Same cmd 51 layout as libnx. Do not initialize/disable the shared radio. */
    const struct {BtdrvAddress address;u8 preference,pad;BtdrvBleConnectionParameter param;} in={oneway_peer,0,0,{interval,interval,0,0,0,400}};
    rc=serviceDispatchIn(&service,51,in);result("Connection interval request (not negotiated-value verification)",rc);
   }
   serviceClose(&service);
  }
  uint8_t out[9]={OW_PARAM_RESULT};memcpy(out+1,p+1,4);lr_put32(out+5,rc);enqueue(out,sizeof(out));
 }
}
static void oneway_tick(void){
 if(!oneway.active || !oneway_send)return;
 uint64_t elapsed=now_ms()-oneway.start,duration=oneway.seconds*1000u;
 unsigned target=(unsigned)((elapsed<duration?elapsed:duration)*oneway.rate/1000);
 for(unsigned i=0;i<32 && oneway.next<target;i++){
  uint8_t p[OW_SIZE];ow_packet(p,oneway.id,oneway.next++);
  if(transport_depth()<LR_QUEUE_DEPTH-4 && enqueue(p,sizeof(p)))oneway.accepted++;else oneway.dropped++;
 }
 if(elapsed>=duration && oneway.next==target){uint8_t end[15]={OW_END};lr_put16(end+1,oneway.id);lr_put32(end+3,oneway.next);lr_put32(end+7,oneway.accepted);lr_put32(end+11,oneway.dropped);if(enqueue(end,sizeof(end)))oneway.active=false;}
}
