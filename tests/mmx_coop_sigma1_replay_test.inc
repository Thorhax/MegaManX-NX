/* Issue110 sessions 7-9 (2.2.7-alpha, Saber co-op, independent views): private
 * reporter checkpoints plus their recorded input pairs, one per line as
 * hexadecimal 12-bit P1/P2 inputs. Each check runs with either seat driving;
 * MMX_COOP_SIGMA1_UNIFIED repeats it with the shared camera. */
static unsigned sigma1_inputs(const char *path,unsigned *p1,unsigned *p2,unsigned cap) {
  FILE *f=path?fopen(path,"r"):NULL;check(f!=NULL,"private Sigma 1 input trace opens");
  unsigned count=0,a,b;
  while(count<cap && fscanf(f,"%x %x",&a,&b)==2) {p1[count]=a;p2[count++]=b;}
  fclose(f);return count;
}
static void sigma1_swap_seats(void) {
  MmxCoopState s=MmxCoopGetState();
  MmxCoopPlayer p=s.players[0];s.players[0]=s.players[1];s.players[1]=p;
  s.current^=1;s.anchor^=1;MmxCoopSetState(&s);
}
static unsigned sigma1_x_seat(const MmxCoopState *s) {
  return s->players[0].character==MMX_COOP_X ? 0 : 1;
}
static const uint8_t *sigma1_body(const MmxCoopState *s,unsigned seat) {
  return seat==s->current ? g_ram+0xba8 : s->players[seat].body;
}
static bool sigma1_forced(const uint8_t *b) { return b[2]==0x1e || b[0x6e]; }
static void sigma1_moves(unsigned seat,const char *what) {
  MmxCoopState s=MmxCoopGetState();unsigned x=coop_x(&s.players[seat]);
  coop_frames(30,seat?0:SNES_PAD_RIGHT,seat?SNES_PAD_RIGHT:0);s=MmxCoopGetState();
  fprintf(stderr,"sigma1 seat=%u x=%u->%u action=%02x lock=%02x\n",seat,x,coop_x(&s.players[seat]),
      sigma1_body(&s,seat)[2],sigma1_body(&s,seat)[0x6e]);
  check(coop_x(&s.players[seat])>x+8,what);
}
static void sigma1_determinism(uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap,const char *what) {
  size_t n=RtlSaveSnapshotToMemory(start,cap);check(n,"Sigma 1 snapshot saves");
  coop_frames(24,0,0);size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"Sigma 1 snapshot restores");
  coop_frames(24,0,0);same(expected,en,actual,RtlSaveSnapshotToMemory(actual,cap),what);
}
/* Session 7: story Zero ($33) locks X with $84:9FEB while he idles, then
 * dashes away and releases with $84:A003. The release went to the partner
 * projected for its nearest-player update, leaving X locked for good. */
static void sigma1_intro_checks(const char *assets,const char *fixture,
    uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap) {
  coop_replay_start(assets);RtlEnableExtendedFrameTiming();
  g_config.no_sprite_limits=true;MmxCoopViewsSetIndependent(!getenv("MMX_COOP_SIGMA1_UNIFIED"));
  static unsigned p1[1200],p2[1200];
  unsigned count=sigma1_inputs(getenv("MMX_COOP_SIGMA1_INPUTS"),p1,p2,1200);
  check(count>=400,"intro trace covers story Zero's departure");
  for(unsigned driver=0;driver<2;++driver) {
    check(RtlLoadSnapshot(fixture),"captured Sigma 1 intro checkpoint loads");
    check(g_ram[0x1f7a]==9 && coop_vile_slot(0x33),"checkpoint precedes story Zero's departure");
    if(driver) sigma1_swap_seats();
    bool gone=false;
    for(unsigned t=0;t<count;++t) {
      coop_frames(1,driver?p2[t]:p1[t],driver?p1[t]:p2[t]);
      gone|=!coop_vile_slot(0x33);
    }
    MmxCoopState s=MmxCoopGetState();unsigned xs=sigma1_x_seat(&s);
    fprintf(stderr,"sigma1 intro driver=%u gone=%u x=%02x:%02x lock=%02x partner=%02x:%02x\n",driver,gone,
        sigma1_body(&s,xs)[2],sigma1_body(&s,xs)[3],sigma1_body(&s,xs)[0x6e],
        sigma1_body(&s,xs^1)[2],sigma1_body(&s,xs^1)[3]);
    check(gone,"story Zero leaves within the recorded inputs");
    check(!sigma1_forced(sigma1_body(&s,xs)) && !sigma1_forced(sigma1_body(&s,xs^1)),
        "story Zero's release frees X, not the partner");
    sigma1_moves(xs,"X walks after story Zero's departure");
    sigma1_moves(xs^1,"partner walks after story Zero's departure");
    sigma1_determinism(start,expected,actual,cap,"Sigma 1 intro replays byte-exactly");
  }
  puts("MMX ISSUE110 SIGMA1 INTRO CHECKS PASSED");
}
/* Session 7's later checkpoint already has X locked and story Zero gone. */
static void sigma1_locked_checks(const char *assets,const char *fixture,
    uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap) {
  coop_replay_start(assets);RtlEnableExtendedFrameTiming();
  g_config.no_sprite_limits=true;MmxCoopViewsSetIndependent(!getenv("MMX_COOP_SIGMA1_UNIFIED"));
  for(unsigned driver=0;driver<2;++driver) {
    check(RtlLoadSnapshot(fixture),"captured locked-X checkpoint loads");
    MmxCoopState s=MmxCoopGetState();unsigned xs=sigma1_x_seat(&s);
    check(sigma1_forced(sigma1_body(&s,xs)) && !coop_vile_slot(0x33),
        "checkpoint holds X's orphaned story lock");
    if(driver) sigma1_swap_seats();
    coop_frames(2,0,0);s=MmxCoopGetState();xs=sigma1_x_seat(&s);
    check(!sigma1_forced(sigma1_body(&s,xs)),"orphaned story lock is released");
    sigma1_moves(xs,"recovered X walks again");
    sigma1_determinism(start,expected,actual,cap,"recovered save replays byte-exactly");
  }
  puts("MMX ISSUE110 SIGMA1 LOCK RECOVERY CHECKS PASSED");
}
/* Sessions 8/9: the view scan spawned armored Vile ($67) before the x=$0A80
 * trigger ($88:EBC1) released the door's camera limit. That trigger then
 * reopened his arena ($1E60=$0F00): s9 walked out of it, and s8's unarmored
 * Vile waited forever at $88:DBF5 for $1E60==$1E5E. Optional inputs replay
 * the recording first (s9: Zero takes the world before Vile arrives). */
static void sigma1_vile_checks(const char *assets,const char *fixture,
    uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap) {
  coop_replay_start(assets);RtlEnableExtendedFrameTiming();
  g_config.no_sprite_limits=true;MmxCoopViewsSetIndependent(!getenv("MMX_COOP_SIGMA1_UNIFIED"));
  static unsigned p1[1200],p2[1200];
  const char *trace=getenv("MMX_COOP_SIGMA1_INPUTS");
  unsigned count=trace?sigma1_inputs(trace,p1,p2,1200):0;
  bool zero_world=getenv("MMX_COOP_SIGMA1_ZERO_WORLD")!=NULL;
  for(unsigned driver=0;driver<2;++driver) {
    check(RtlLoadSnapshot(fixture),"captured Sigma 1 door checkpoint loads");
    check(g_ram[0x1f7a]==9 && !coop_vile_slot(0x67),"checkpoint precedes armored Vile");
    if(driver) sigma1_swap_seats();
    bool spawned=false,captured=false,rescued=false,returned=false,leaked=false;
    unsigned spawn_world=0;
    for(unsigned t=0;t<9000;++t) {
      MmxCoopState s=MmxCoopGetState();
      unsigned vile=coop_vile_slot(0x67),zero=coop_vile_slot(0x66),final=coop_vile_slot(0x69);
      if(vile && !spawned) {
        spawned=true;spawn_world=s.players[s.anchor].character;
        fprintf(stderr,"sigma1 vile driver=%u tick=%u world=%u camera=%u bounds=%04x/%04x\n",driver,t,
            spawn_world,eagle_word(g_ram+0x1e4d),eagle_word(g_ram+0x1e5e),eagle_word(g_ram+0x1e60));
      }
      /* From his spawn until his unarmored form is defeated, the arena stays
       * locked at $0A80 ($83:E842); only $88:E235 releases it. */
      if(((vile && g_ram[vile+1]) || final) && !(final && g_ram[final+1]>=6) &&
          (eagle_word(g_ram+0x1e5e)!=0xa80 || eagle_word(g_ram+0x1e60)!=0xa80)) {
        if(!leaked) fprintf(stderr,"sigma1 arena opened tick=%u bounds=%04x/%04x\n",t,
            eagle_word(g_ram+0x1e5e),eagle_word(g_ram+0x1e60));
        leaked=true;
      }
      captured|=vile && g_ram[vile+1]==2 && g_ram[vile+2]==0x16;
      rescued|=captured && zero && g_ram[zero+2]>=14;
      if(captured && rescued && final && g_ram[final+1]==4 && !s.scene_owner && !g_ram[0xc16]) {
        returned=true;break;
      }
      unsigned pads[2]={0,0};
      if(t<count) {pads[0]=driver?p2[t]:p1[t];pads[1]=driver?p1[t]:p2[t];}
      else {
        unsigned pad=eagle_word(g_ram+0x1e4d)<2688 ? SNES_PAD_RIGHT : 0;
        if(g_ram[0x1f3b] && t%30==0) pad|=SNES_PAD_B|SNES_PAD_Y|SNES_PAD_A;
        pads[s.anchor]=pad;
      }
      coop_frames(1,pads[0],pads[1]);
    }
    MmxCoopState s=MmxCoopGetState();
    fprintf(stderr,"sigma1 vile driver=%u spawned=%u captured=%u rescued=%u returned=%u leaked=%u final=%02x:%02x lock=%02x bounds=%04x/%04x\n",
        driver,spawned,captured,rescued,returned,leaked,coop_vile_slot(0x69)?g_ram[coop_vile_slot(0x69)+1]:0,
        coop_vile_slot(0x69)?g_ram[coop_vile_slot(0x69)+2]:0,g_ram[0xc16],eagle_word(g_ram+0x1e5e),eagle_word(g_ram+0x1e60));
    check(spawned && !leaked,"armored Vile's arena lock survives the x=$0A80 trigger");
    if(zero_world) check(spawn_world==MMX_COOP_ZERO,"Zero holds the world when Vile arrives");
    check(captured && rescued && returned,"capture, rescue and unarmored Vile's entrance complete");
    check(s.players[0].status==MMX_COOP_ALIVE && s.players[1].status==MMX_COOP_ALIVE,
        "both players return for unarmored Vile");
    sigma1_determinism(start,expected,actual,cap,"Vile encounter replays byte-exactly");
    /* Unarmored Vile's paralysis shot ($2F) locks its target ($88:8505);
     * decor $3F releases the world actor ($88:E3D7). The partner advances on
     * him while the world actor stands back: no seat may stay held. */
    unsigned held[2]={0,0},worst[2]={0,0},paralysed=0;
    for(unsigned t=0;t<1800;++t) {
      s=MmxCoopGetState();
      unsigned partner=s.anchor^1,pads[2]={0,0};
      unsigned final=coop_vile_slot(0x69);
      if(!final || g_ram[final+1]>=6) break;
      int dx=(int)eagle_word(g_ram+final+5)-(int)coop_x(&s.players[partner]);
      pads[partner]=(dx>24 ? SNES_PAD_RIGHT : dx<-24 ? SNES_PAD_LEFT : 0)|(t%40<20 ? SNES_PAD_Y : 0);
      coop_frames(1,pads[0],pads[1]);s=MmxCoopGetState();
      for(unsigned seat=0;seat<2;++seat) {
        const uint8_t *b=sigma1_body(&s,seat);
        bool lock=s.players[seat].status==MMX_COOP_ALIVE && b[0x6e]==0x1e && b[2]==0x1e;
        held[seat]=lock?held[seat]+1:0;if(held[seat]>worst[seat]) worst[seat]=held[seat];
        paralysed+=lock && held[seat]==1;
      }
      if(s.players[0].status!=MMX_COOP_ALIVE || s.players[1].status!=MMX_COOP_ALIVE) break;
    }
    fprintf(stderr,"sigma1 vile fight driver=%u paralyses=%u longest=%u/%u\n",driver,paralysed,worst[0],worst[1]);
    check(worst[0]<300 && worst[1]<300,"Vile's paralysis releases the body it held");
  }
  puts("MMX ISSUE110 SIGMA1 VILE CHECKS PASSED");
}
