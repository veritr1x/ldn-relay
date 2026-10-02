/* MIT. Exact 120-byte synthetic payloads plus the relay's 10-byte UDP envelope
 * budget. RTT is measured on Switch's monotonic clock; no clock sync assumed. */
static struct {
    bool active;uint32_t rate,seconds,next,enqueued,echoed,dropped,corrupt,max;
    uint64_t start,sum;uint32_t histogram[2001];uint8_t seen[15000];uint32_t duplicates;
} bench;
static void bench_begin(const uint8_t *p,size_t n,uint16_t id){
    if(!stream_mode || (n!=4 && n!=8) || !lr_get16(p) || lr_get16(p)>1000 || !lr_get16(p+2) || lr_get16(p+2)>120 || bench.active){error_reply(id,RL_BENCH,RL_ERR_STATE,0);return;}
    if(n==8){
        unsigned frame=lr_get16(p+4),window=lr_get16(p+6);
        if(usb_mode || frame<64 || frame>codec.frame_limit || window<1 || window>LS_WINDOW || batch.size>3 || transport_depth()){
            error_reply(id,RL_BENCH,RL_ERR_STATE,0);return;
        }
        atomic_store(&stream_tuning,(frame<<8)|window);
        lr_batch_init(&batch,frame-LS_HEADER);
        note("BENCH TUNING frame=%u window=%u",frame,window);
    }
    memset(&bench,0,sizeof(bench));bench.active=true;bench.rate=lr_get16(p);bench.seconds=lr_get16(p+2);bench.start=now_ms();
    note("BENCH START rate=%u seconds=%u payload=120 envelope=130 mode=stream-v2 LDN=%u",bench.rate,bench.seconds,joined);
}
static void bench_echo(const uint8_t *p,size_t n){
    if(!bench.active || n!=130)return;
    uint32_t seq=lr_get32(p+3);
    if(seq>=bench.next){bench.corrupt++;return;}
    for(size_t i=15;i<n;i++)if(p[i]!=(uint8_t)(seq+i*17)){bench.corrupt++;return;}
    uint64_t stamp=lr_get64(p+7),now=now_ms();if(stamp<bench.start || stamp>now){bench.corrupt++;return;}
    if(bench.seen[seq/8] & (1u<<(seq%8))){bench.duplicates++;return;}
    bench.seen[seq/8]|=(uint8_t)(1u<<(seq%8));
    uint32_t rtt=(uint32_t)(now-stamp);bench.echoed++;bench.sum+=rtt;if(rtt>bench.max)bench.max=rtt;bench.histogram[rtt>2000?2000:rtt]++;
}
static void bench_tick(void){
    if(!bench.active)return;
    uint64_t elapsed=now_ms()-bench.start,duration=(uint64_t)bench.seconds*1000;
    uint32_t target=(uint32_t)((elapsed<duration?elapsed:duration)*bench.rate/1000);
    for(unsigned i=0;i<32 && bench.next<target;i++){
        uint32_t seq=bench.next++;uint8_t p[130]={RL_BENCH_DATA};lr_put32(p+3,seq);lr_put64(p+7,now_ms());
        for(size_t j=15;j<sizeof(p);j++)p[j]=(uint8_t)(seq+j*17);
        if(transport_depth()<LR_QUEUE_DEPTH-4 && enqueue(p,sizeof(p)))bench.enqueued++;else bench.dropped++;
    }
    if(elapsed<duration || (bench.echoed<bench.enqueued && elapsed<duration+5000))return;
    uint32_t percentile=0,sum=0,threshold=(bench.echoed*95+99)/100;
    for(unsigned i=0;i<=2000;i++){sum+=bench.histogram[i];if(sum>=threshold){percentile=i;break;}}
    uint8_t report[43]={RL_BENCH_DONE};uint32_t values[10]={bench.rate,bench.next,bench.enqueued,bench.echoed,bench.dropped,bench.corrupt,bench.echoed?(uint32_t)(bench.sum/bench.echoed):0,percentile,bench.max,1};
    for(unsigned i=0;i<10;i++)lr_put32(report+3+i*4,values[i]);
    if(!enqueue(report,sizeof(report)))return;
    bench.active=false;note("BENCH DONE rate=%u planned=%u enqueued=%u echoed=%u drop=%u corrupt=%u rtt_avg_ms=%u p95_ms=%u max_ms=%u duplicates=%u",values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7],values[8],bench.duplicates);
}
