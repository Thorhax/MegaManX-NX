/* Regressions from public report #124, using original controllers and menus. */
static void rush_report_pair(unsigned a,unsigned b) {
  MmxBossRushState s=MmxBossRushGetState();
  memset(s.bosses,0,sizeof(s.bosses));memset(s.owner,0,sizeof(s.owner));
  memset(g_ram+0xe68,0,0x3c0);memset(g_ram+0x1428,0,0x8e0);
  s.queue[0]=(uint8_t)a;s.queue[1]=(uint8_t)b;s.queued=8;
  unsigned n=2;for(unsigned i=0;i<8;++i)if(i!=a && i!=b)s.queue[n++]=(uint8_t)i;
  check(MmxBossRushSetState(&s),"report fixture queues its native bosses");
  for(unsigned t=0;t<1500;++t) {
    frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
    if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING)break;
  }
  check(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING,
      "report fixture completes both native entrances");
}
static void rush_report_player(unsigned x,unsigned y) {
  eagle_putword(g_ram+0xbad,x);eagle_putword(g_ram+0xbb0,y);
  g_ram[0xbac]=g_ram[0xbaf]=g_ram[0xbaa]=g_ram[0xbab]=0;
  g_ram[0xbcf]=0xa0;g_ram[0xbd8]=g_ram[0xc13]=g_ram[0xc16]=0;
  g_ram[0xc30]=g_ram[0xc32]=0;
  if(MmxCoopEnabled())MmxCoopCapture(g_ram);
}
static void rush_report_checks(uint8_t *arena,uint8_t *pair,size_t cap) {
  size_t empty=RtlSaveSnapshotToMemory(arena,cap);check(empty,"report empty arena saves");
  bool victory=true,tanks=true,freeze=true,teleport=true;
  /* Each controller gets a real update with lethal HP, rather than invoking
   * host defeat bookkeeping directly. Victory scripts must not grant X immunity. */
  for(unsigned boss=0;boss<8;++boss)for(unsigned seat=0;seat<2;++seat) {
    check(RtlLoadSnapshotFromMemory(arena,empty),"report arena restores before defeat");
    rush_report_pair(seat? (boss?0:5):boss,seat?boss:(boss?0:5));
    MmxBossRushState s=MmxBossRushGetState();
    rush_report_player(s.camera_x+24,s.camera_y+164);
    for(unsigned i=0;i<2;++i)eagle_putword(g_ram+s.bosses[i].object+5,s.camera_x+300);
    g_ram[s.bosses[seat].object+0x27]=0;
    frame(0);
    bool ok=!g_ram[0xbd8] || g_ram[0xc13];victory&=ok;
    fprintf(stderr,"report124 defeat boss=%u seat=%u latch=%u blink=%u pass=%u\n",
        boss,seat,g_ram[0xbd8],g_ram[0xc13],ok);
  }
  check(RtlLoadSnapshotFromMemory(arena,empty),"report arena restores for menu");
  MmxBossRushState s=MmxBossRushGetState();
  unsigned initial=g_ram[0x1f83];
  check(g_snes->cart->rom[0x164fa]==0xc9 && g_snes->cart->rom[0x164fb]==0x8e,
      "original pickup refill independently supplies the native tank cap");
  tanks&=initial==0x8e; /* The original pickup refill stops at $8E. */
  for(unsigned i=1;i<4;++i)g_ram[0x1f83+i]=0x80;
  g_ram[0xbcf]=0x84;
  frame(SNES_PAD_START);zero_replay(90);
  g_ram[0x1ed2]=g_ram[0x1ed3]=10; /* The initially full first tank is selectable. */
  frame(SNES_PAD_Y);zero_replay(150);
  unsigned spent=g_ram[0x1f83],healed=g_ram[0xbcf]&127;
  tanks&=spent==0x80 && healed==32;
  g_ram[0xbcf]=0x84;
  bool selected_empty=false;
  for(unsigned direction=0;direction<4;++direction)for(unsigned t=0;t<16;++t) {
    const unsigned keys[]={SNES_PAD_UP,SNES_PAD_DOWN,SNES_PAD_LEFT,SNES_PAD_RIGHT};
    frame(keys[direction]);zero_replay(5);
    if(g_ram[0x1ed2]>=10) {
      selected_empty=true;frame(SNES_PAD_Y);zero_replay(120);break;
    }
  }
  tanks&=!selected_empty && (g_ram[0xbcf]&127)==4;
  g_ram[0x1f83]=0x90;g_ram[0x1ed2]=g_ram[0x1ed3]=10;
  frame(SNES_PAD_Y);zero_replay(120);
  tanks&=g_ram[0x1f83]==0x80 && (g_ram[0xbcf]&127)==4;
  fprintf(stderr,"report124 tank initial=%02x spent=%02x healed=%u selected_empty=%u final_hp=%u pass=%u\n",
      initial,spent,healed,selected_empty,g_ram[0xbcf]&127,tanks);

  check(RtlLoadSnapshotFromMemory(arena,empty),"report arena restores for ice and teleport");
  rush_report_pair(0,4);s=MmxBossRushGetState();
  size_t saved=RtlSaveSnapshotToMemory(pair,cap);check(saved,"Penguin and Kuwanger report pair saves");
  rush_report_player(s.camera_x+160,s.camera_y+164);
  for(unsigned i=0;i<2;++i)eagle_putword(g_ram+s.bosses[i].object+5,s.camera_x+300);
  memset(g_ram+0x1928,0,0x3e0);
  unsigned ice=0x1928;g_ram[ice]=1;g_ram[ice+10]=8;
  eagle_putword(g_ram+ice+12,s.bosses[0].object);
  eagle_putword(g_ram+ice+5,s.camera_x+160);eagle_putword(g_ram+ice+8,s.camera_y+164);
  MmxBossRushSetOwner(ice,0);
  bool frozen=false;unsigned frozen_x=0;
  for(unsigned t=0;t<120;++t) {
    frame(SNES_PAD_RIGHT);
    if(g_ram[ice] && g_ram[ice+1]>=2) {
      if(!frozen){frozen=true;frozen_x=eagle_word(g_ram+0xbad);}
      freeze&=g_ram[0xc16]!=0 && eagle_word(g_ram+0xbad)==frozen_x;
    }
  }
  freeze&=frozen;
  fprintf(stderr,"report124 ice frozen=%u x=%04x pass=%u\n",frozen,frozen_x,freeze);

  for(unsigned trial=0;trial<32;++trial) {
    check(RtlLoadSnapshotFromMemory(pair,saved),"teleport report pair restores");
    s=MmxBossRushGetState();unsigned d=s.bosses[1].object;
    rush_report_player(s.camera_x+(trial&1?344:16),s.camera_y+164);
    eagle_putword(g_ram+s.bosses[0].object+5,s.camera_x+300);
    eagle_putword(g_ram+d+5,s.camera_x+184);eagle_putword(g_ram+d+8,s.camera_y+164);
    g_ram[d+1]=4;g_ram[d+2]=4;g_ram[d+3]=0;
    eagle_putword(g_ram+0xba6,trial*147+1);
    bool stuck=false;
    for(unsigned t=0;t<150;++t) {
      frame(0);rush_keep_players_alive();
      unsigned target=eagle_word(g_ram+d+0x3a),x=eagle_word(g_ram+d+5);
      if(g_ram[d+2]==4 && g_ram[d+3]==4 &&
          (target<s.camera_x+32 || target>s.camera_x+336))stuck=true;
      if(t==149 && stuck)fprintf(stderr,"report124 teleport trial=%u x=%04x target=%04x state=%u:%u\n",
          trial,x,target,g_ram[d+2],g_ram[d+3]);
    }
    teleport&=!stuck;
  }
  check(RtlLoadSnapshotFromMemory(pair,saved),"natural Kuwanger report pair restores");
  s=MmxBossRushGetState();unsigned kuwanger=s.bosses[1].object,min_x=65535,max_x=0;
  /* A legacy snapshot can already contain both an unreachable destination
   * and a boss behind the closed door. Resume his own attack to completion. */
  eagle_putword(g_ram+kuwanger+5,s.camera_x-32);
  eagle_putword(g_ram+kuwanger+0x3a,s.camera_x-64);
  g_ram[kuwanger+2]=g_ram[kuwanger+3]=4;
  for(unsigned t=0;t<180;++t){frame(0);rush_keep_players_alive();}
  check(eagle_word(g_ram+kuwanger+5)>=s.camera_x+32 &&
      !(g_ram[kuwanger+2]==4 && g_ram[kuwanger+3]==4),
      "Kuwanger recovers from an existing behind-door position and resumes his AI");
  check(RtlLoadSnapshotFromMemory(pair,saved),"natural edge combat restores after legacy recovery");
  g_ram[kuwanger+0x27]=8;
  for(unsigned t=0;t<4800;++t) {
    frame(t<2400?SNES_PAD_LEFT:SNES_PAD_RIGHT);rush_keep_players_alive();
    unsigned x=eagle_word(g_ram+kuwanger+5);
    if(x<min_x)min_x=x;if(x>max_x)max_x=x;
    if(x<s.camera_x+32 || x>s.camera_x+336) {
      teleport=false;
    }
  }
  fprintf(stderr,"report124 Kuwanger natural bounds=%04x/%04x pass=%u\n",min_x,max_x,teleport);
  check(victory,"boss defeats do not leak permanent player immunity");
  check(tanks,"retail subtank units heal once and empty tanks cannot be selected");
  check(freeze,"native Penguin ice retains the player movement lock");
  check(teleport,"Kuwanger teleport targets remain inside the arena walls");
  puts("MMX BOSS RUSH REPORT124 CHECKS PASSED");
}
