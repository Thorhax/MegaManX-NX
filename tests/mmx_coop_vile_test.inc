/* Private ROM-backed replay from the Sigma 1 door checkpoint. */
static unsigned coop_vile_slot(unsigned kind) {
  for(unsigned d=0xe68;d<0x1228;d+=64)
    if(g_ram[d] && g_ram[d+10]==kind) return d;
  return 0;
}
/* Owner checkpoint before the Highway ship finishes deploying cars. Keep the
 * actual actors, sound processor and co-op contexts from the save: fabricating
 * Vile alone misses the ship's earlier music-bank handoff. */
static void coop_highway_vile_checks(const char *fixture) {
  check(MmxCoopEnable(0),"Highway Vile co-op enables");
  check(RtlLoadSnapshot(fixture),"private Highway traffic fixture loads");
  g_mmx_custom_renderer=true;g_mmx_custom_aspect=MMX_ASPECT_16_9;
  g_mmx_expanded_sprites=true;int w,h;MmxPrepareFrame(1280,720,&w,&h);
  check(g_ram[0x1f7a]==0 && coop_vile_slot(0x1a),"fixture contains the Highway ship");
  MmxCoopState s=MmxCoopGetState();
  check(s.players[0].status==MMX_COOP_ALIVE && s.players[1].status==MMX_COOP_ALIVE,
      "both players are present before the encounter");
  bool vile=false,rescue=false,transport=false,cleared=false,returned=false;
  for(unsigned tick=0;tick<6500;++tick) {
    vile|=coop_vile_slot(0x32)!=0;
    rescue|=coop_vile_slot(0x33)!=0;
    s=MmxCoopGetState();transport|=s.scene_phase==2;
    if(g_ram[0x1f7a]!=0) cleared=true;
    if(cleared && g_ram[0xd1]==2 && g_ram[0xd2]==4 && g_ram[0xd3]==4 &&
        !s.scene_owner && !g_ram[0xc16] &&
        s.players[0].status==MMX_COOP_ALIVE && s.players[1].status==MMX_COOP_ALIVE &&
        s.players[0].body[2]<4 && s.players[1].body[2]<4 &&
        !s.players[0].zero.swap_phase && !s.players[1].zero.swap_phase) {
      returned=true;break;
    }
    /* Both fire through the cars and fight. Periodic face-button edges also
     * advance the native conversation and stage selection after the rescue. */
    unsigned pad=tick%180<160 ? SNES_PAD_Y : 0;
    if(g_ram[0x1f3b] && tick%30==0) pad|=SNES_PAD_A|SNES_PAD_B;
    coop_frames(1,pad,pad);
  }
  check(vile && rescue && transport && cleared && returned,
      "Highway Vile arrives, captures, is interrupted by Zero, and both players enter the next stage");
  unsigned x0=coop_x(&s.players[0]),x1=coop_x(&s.players[1]);
  coop_frames(12,SNES_PAD_RIGHT,SNES_PAD_RIGHT);s=MmxCoopGetState();
  check(coop_x(&s.players[0])>x0 && coop_x(&s.players[1])>x1,
      "both players regain control after the Highway encounter");
  MmxCoopDisable();puts("MMX CO-OP HIGHWAY VILE CHECKS PASSED");
}
static void coop_vile_farewell_checks(const char *fixture) {
  for(unsigned xs=0;xs<2;++xs) {
    check(MmxCoopEnable(0),"farewell roster enables");
    check(RtlLoadSnapshot(fixture),"private post-Vile fixture loads");
    MmxCoopCapture(g_ram);MmxCoopState s=MmxCoopGetState();
    if(xs) {MmxCoopPlayer p=s.players[0];s.players[0]=s.players[1];s.players[1]=p;}
    s.current=s.anchor=(uint8_t)(xs^1);s.scene_owner=s.scene_phase=s.stage_pending=0;
    for(unsigned seat=0;seat<2;++seat) {
      MmxCoopPlayer *p=&s.players[seat];unsigned x=seat==xs ? 2940 : 3020;
      p->body[5]=(uint8_t)x;p->body[6]=(uint8_t)(x>>8);
      p->body[8]=159;p->body[9]=5;p->body[2]=p->body[3]=0;
      p->zero.swap_phase=0;p->body[0x27]=0x98;p->status=MMX_COOP_ALIVE;
    }
    memcpy(g_ram+0xba8,s.players[s.current].body,sizeof(s.players[0].body));
    MmxZeroSetState(s.players[s.current].zero);MmxCoopSetState(&s);
    unsigned npc=coop_vile_slot(0x66);
    check(npc && g_ram[npc+2]==14 && g_ram[npc+3]==2,"story Zero awaits X");
    coop_frames(40,0,0);s=MmxCoopGetState();
    check(g_ram[npc+3]==2 && !g_ram[0x1f3b] && !s.scene_owner && s.anchor==(xs^1),
        "Zero crosses farewell trigger without starting X's conversation");
    bool started=false;
    for(unsigned tick=0;tick<100;++tick) {
      coop_frames(1,xs ? 0 : SNES_PAD_RIGHT,xs ? SNES_PAD_RIGHT : 0);
      s=MmxCoopGetState();
      if(g_ram[npc+3]>=4) {started=true;break;}
    }
    check(started && s.anchor==xs && s.scene_owner==xs+1,
        "X in either seat triggers and owns the farewell while Zero drives the world");
    check(s.players[xs].character==MMX_COOP_X && s.players[xs^1].zero.swap_phase,
        "farewell transports playable Zero, retaining X");
    bool returned=false;
    for(unsigned tick=0;tick<2000;++tick) {
      unsigned pad=g_ram[0x1f3b] && tick%30==0 ? SNES_PAD_B|SNES_PAD_Y|SNES_PAD_A : 0;
      coop_frames(1,xs ? 0 : pad,xs ? pad : 0);s=MmxCoopGetState();
      if(!s.scene_owner && !g_ram[0x1f3b] && (!g_ram[npc] || g_ram[npc+3]>=10)) {
        returned=true;break;
      }
    }
    check(returned && s.players[0].status==MMX_COOP_ALIVE && s.players[1].status==MMX_COOP_ALIVE,
        "farewell completes and both players return alive");
  }
  MmxCoopDisable();puts("MMX CO-OP VILE FAREWELL CHECKS PASSED");
}
static void coop_vile_checks(const char *fixture) {
  if(getenv("MMX_COOP_VILE_FAREWELL_TEST")) {coop_vile_farewell_checks(fixture);return;}
  unsigned roster=getenv("MMX_COOP_VILE_ZERO_WORLD") ? 1 : 0;
  check(MmxCoopEnable(roster),"Vile co-op roster enables");
  check(RtlLoadSnapshot(fixture),"private Sigma 1 door fixture loads");
  /* This checkpoint predates co-op. Match its native character context to
   * the requested roster before Initialize captures the world actor. */
  MmxZeroState z=MmxZeroGetState();z.active_x=roster==MMX_COOP_X;MmxZeroSetState(z);
  bool captured=false,hidden=false,rescued=false,returned=false;
  unsigned partner_hp=0,partner_weapon=0;
  for(unsigned tick=0;tick<7000;++tick) {
    MmxCoopState s=MmxCoopGetState();
    unsigned vile=coop_vile_slot(0x67),zero=coop_vile_slot(0x66);
    if(vile && g_ram[vile+1]==2 && g_ram[vile+2]==0x16 && !captured) {
      captured=true;partner_hp=s.players[s.anchor^1].body[0x27]&127;
      partner_weapon=s.players[s.anchor^1].body[0x33];
      check(s.players[s.anchor^1].status==MMX_COOP_ALIVE,"partner alive before Vile capture");
      check(RtlSaveSnapshot("vile-capture.sav"),"private pre-capture fixture saves");
    }
    if(captured && s.scene_phase==2) {
      if(!hidden) check(s.scene_owner==s.anchor+1,"Vile capture belongs to the world actor");
      hidden=true;
      if((s.players[s.anchor^1].body[0x27]&127)!=partner_hp ||
          s.players[s.anchor^1].body[0x33]!=partner_weapon ||
          s.players[s.anchor^1].body[2]==0x3c)
        check(false,"hidden partner retains health and avoids Vile restraint");
    }
    if(captured && zero && g_ram[zero+2]>=14) rescued=true;
    unsigned final=coop_vile_slot(0x69);
    if(captured && hidden && rescued && final && g_ram[final+1]==4 &&
        !s.scene_owner && !g_ram[0xc16]) {
      returned=true;
      check(s.players[0].status==MMX_COOP_ALIVE && s.players[1].status==MMX_COOP_ALIVE &&
          (s.players[s.anchor^1].body[0x27]&127)==partner_hp,
          "both players return alive for unarmored Vile");
      unsigned x0=coop_x(&s.players[0]),x1=coop_x(&s.players[1]);
      coop_frames(12,SNES_PAD_LEFT,SNES_PAD_RIGHT);s=MmxCoopGetState();
      check(coop_x(&s.players[0])<x0 && coop_x(&s.players[1])>x1,
          "both players regain movement after the rescue");
      check(RtlSaveSnapshot("vile-final-fight.sav"),"private playable final-fight fixture saves");
      break;
    }
    unsigned pad=(g_ram[0x1e4d]|g_ram[0x1e4e]<<8)<2688 ? SNES_PAD_RIGHT : 0;
    if(g_ram[0x1f3b] && tick%30==0) pad|=SNES_PAD_B|SNES_PAD_Y|SNES_PAD_A;
    coop_frames(1,pad,0);
  }
  if(!returned) {
    MmxCoopState s=MmxCoopGetState();
    fprintf(stderr,"Vile incomplete: capture=%u hidden=%u rescue=%u scene=%u/%u action=%02x:%02x lock=%02x HP=%u/%u\n",
        captured,hidden,rescued,s.scene_owner,s.scene_phase,g_ram[0xbaa],g_ram[0xbab],g_ram[0xc16],g_ram[0xbcf]&127,g_ram[0x1f9a]);
    RtlSaveSnapshot("vile-incomplete.sav");
  }
  check(captured && hidden && rescued && returned,"Sigma 1 capture, rescue and partner return complete");
  MmxCoopDisable();puts("MMX CO-OP VILE CHECKS PASSED");
}
