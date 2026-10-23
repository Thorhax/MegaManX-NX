/* Exercise the native fatal contact and death controller before testing the
 * survivor. Never reset the survivor's hurt latch after the fatal hit. */
static void rush_coop_isolation_checks(uint8_t *arena,size_t cap) {
  check(MmxCoopEnabled(),"isolation checks require two native controllers");
  size_t empty=RtlSaveSnapshotToMemory(arena,cap);
  FILE *log=fopen("rush-coop-isolation.csv","w");check(log!=NULL,"isolation trace opens");
  fputs("boss,dead,frame,p1_hp,p2_hp,p1_action,p2_action,p1_immune,p2_immune,freeze,boss0_damage,boss1_damage\n",log);
  /* Both projections need recovery, but legitimate native protection must
   * retain its owner and duration. These are explicit state-machine fixtures. */
  for(unsigned guard=0;guard<5;++guard) {
    check(RtlLoadSnapshotFromMemory(arena,empty),"hurt guard arena restores");
    for(unsigned seat=0;seat<2;++seat) {
      MmxCoopSelect(g_ram,seat);
      g_ram[0xbd8]=g_ram[0xc32]=1;g_ram[0xc13]=guard==1?17:0;
      g_ram[0xbaa]=guard==2?14:0;g_ram[0xc16]=guard==3?1:0;g_ram[0xc31]=guard==4?1:0;
      MmxCoopCapture(g_ram);
    }
    MmxCoopSelect(g_ram,0);MmxCoopResumeHurtTimers(g_ram);MmxCoopCapture(g_ram);
    MmxCoopState c=MmxCoopGetState();
    for(unsigned seat=0;seat<2;++seat)
      check(c.players[seat].body[0x6b]==(guard==0?60:guard==1?17:0),
          "hurt recovery respects both seats' active timers, hurt actions, scripts and charged Sting");
  }
  bool passed=true,recovered=true;
  for(unsigned boss=0;boss<8;++boss)for(unsigned order=0;order<2;++order) {
    unsigned dead=order^1;
    check(RtlLoadSnapshotFromMemory(arena,empty),"isolation arena restores");
    MmxBossRushState s=MmxBossRushGetState();
    memset(s.bosses,0,sizeof(s.bosses));memset(s.owner,0,sizeof(s.owner));
    memset(g_ram+0xe68,0,0x3c0);memset(g_ram+0x1428,0,0x8e0);
    s.queue[0]=(uint8_t)boss;s.queue[1]=(uint8_t)(boss==7?0:7);s.queued=8;
    unsigned q=2;for(unsigned i=0;i<8;++i)if(i!=s.queue[0] && i!=s.queue[1])s.queue[q++]=(uint8_t)i;
    check(MmxBossRushSetState(&s),"isolation selects each Maverick");
    for(unsigned t=0;t<1500;++t) {
      frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
      if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING)break;
    }
    check(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING,
        "isolation completes native boss introductions");
    unsigned x=s.camera_x+240,y=s.camera_y+164;
    rush_coop_player(dead^1,s.camera_x+48,y,0);
    rush_coop_player(dead,x,y,0);
    g_ram[0xbcf]=0x81;MmxCoopCapture(g_ram);MmxCoopSelect(g_ram,0);
    bool fatal=false,hurt=false,fallen=false;
    unsigned after_death=0;
    for(unsigned t=0;t<360;++t) {
      memset(g_ram+0x1428,0,0x8e0);
      MmxCoopState c=MmxCoopGetState();
      unsigned survivor=dead^1;
      if(fallen) ++after_death;
      const uint8_t *b=c.players[fallen?survivor:dead].body;
      rush_coop_targets(0,fallen?eagle_word(b+5):x,y);
      frame(0);c=MmxCoopGetState();
      fatal|=!(c.players[dead].body[0x27]&127);
      fallen|=c.players[dead].status==MMX_COOP_FALLEN;
      hurt|=fallen && (c.players[survivor].body[0x27]&127)<32;
      fprintf(log,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",boss,dead,t,
          c.players[0].body[0x27]&127,c.players[1].body[0x27]&127,
          c.players[0].body[2],c.players[1].body[2],
          c.players[0].body[0x8a],c.players[1].body[0x8a],g_ram[0x1f0c],
          g_ram[s.bosses[0].object+0x26],g_ram[s.bosses[1].object+0x26]);
      if(hurt || after_death>120)break;
    }
    fprintf(stderr,"isolation boss=%u dead=%u fatal=%u fallen=%u survivor_hurt=%u damage=%u/%u\n",
        boss,dead,fatal,fallen,hurt,g_ram[s.bosses[0].object+0x26],g_ram[s.bosses[1].object+0x26]);
    passed&=fatal && fallen && hurt;
    if(boss==0 && fallen) {
      /* Regression state of a hit interrupted before the native hurt
       * reaction starts its blink timer (for example by a health refill).
       * Keep this separate from the real fatal-contact checks above. */
      unsigned survivor=dead^1;
      rush_coop_targets(0,s.camera_x-128,y);
      for(unsigned t=0;t<120;++t)frame(0);
      MmxCoopSelect(g_ram,survivor);
      g_ram[0xbaa]=g_ram[0xbab]=0;
      g_ram[0xbd8]=g_ram[0xc32]=1;g_ram[0xc13]=0;
      MmxCoopCapture(g_ram);
      unsigned hp=g_ram[0xbcf]&127;
      bool cleared=false,damaged=false;
      for(unsigned t=0;t<240;++t) {
        MmxCoopState c=MmxCoopGetState();
        const uint8_t *b=c.players[survivor].body;
        rush_coop_targets(0,cleared?eagle_word(b+5):s.camera_x-128,y);
        frame(0);c=MmxCoopGetState();b=c.players[survivor].body;
        cleared|=!b[0x30] && !b[0x8a];
        damaged|=(b[0x27]&127)<hp;
        if(damaged)break;
      }
      fprintf(stderr,"stale hurt dead=%u cleared=%u survivor_hurt=%u\n",dead,cleared,damaged);
      recovered&=cleared && damaged;
    }
  }
  fclose(log);
  check(passed,"all Mavericks can damage either survivor after the partner's native death");
  check(recovered,"interrupted hurt protection expires and either survivor can be damaged again");
  puts("MMX BOSS RUSH CO-OP ISOLATION CHECKS PASSED");
}

static void rush_coop_weapon_protect(void) {
  unsigned current=MmxCoopGetState().current;
  for(unsigned seat=0;seat<2;++seat) {
    MmxCoopSelect(g_ram,seat);
    g_ram[0xbd8]=g_ram[0xc32]=8;g_ram[0xc13]=60;
    MmxCoopCapture(g_ram);
  }
  MmxCoopSelect(g_ram,current);
}
static void rush_coop_weapon_checks(uint8_t *arena,size_t cap) {
  check(MmxCoopEnabled(),"weapon isolation checks require co-op");
  size_t empty=RtlSaveSnapshotToMemory(arena,cap);
  FILE *log=fopen("rush-coop-weapons.csv","w");check(log!=NULL,"weapon trace opens");
  bool graphics=true;unsigned seen[2]={0};
  fputs("weapon,charged,seat,frame,slot,kind,group,pose,attributes,tilebase,dma\n",log);
  for(unsigned weapon=1;weapon<=8;++weapon)for(unsigned charged=0;charged<2;++charged) {
    check(RtlLoadSnapshotFromMemory(arena,empty),"weapon arena restores");
    MmxBossRushState s=MmxBossRushGetState();
    rush_coop_player(0,s.camera_x+80,s.camera_y+164,0);
    rush_coop_player(1,s.camera_x+192,s.camera_y+164,0);
    MmxCoopSelect(g_ram,0);
    /* Use native quick switching so both real CHR/palette uploads run,
     * including the shared-residency conflict this regression addresses. */
    for(unsigned step=0;step<9;++step) {
      MmxCoopState c=MmxCoopGetState();
      unsigned keys=(c.players[0].body[0x33]/2!=weapon?SNES_PAD_R:0)|
          (c.players[1].body[0x33]/2!=weapon%8+1?SNES_PAD_R<<12:0);
      if(!keys)break;
      for(unsigned t=0;t<12;++t) {
        rush_coop_weapon_protect();
        rush_coop_targets(0,s.camera_x-128,s.camera_y+164);frame(t?0:keys);
      }
    }
    MmxCoopState selected=MmxCoopGetState();
    check(selected.players[0].body[0x33]/2==weapon && selected.players[1].body[0x33]/2==weapon%8+1,
        "both seats select different specials through native quick switching");
    for(unsigned t=0;t<300;++t) {
      /* Protection isolates graphics from Mammoth's room-wide stomp and
       * stray boss projectiles; damage is measured separately above. */
      rush_coop_weapon_protect();
      rush_coop_targets(0,s.camera_x-128,s.camera_y+164);
      unsigned input=charged?(t<220?SNES_PAD_Y:0):(t%30==0?SNES_PAD_Y:0);
      frame(input|(input<<12));
      MmxCoopCapture(g_ram);MmxCoopState c=MmxCoopGetState();
      for(unsigned seat=0;seat<2;++seat) for(unsigned slot=0;slot<8;++slot) {
        const uint8_t *b=c.players[seat].shots+slot*64;
        if(b[0] && (b[14]&128)) {
          fprintf(log,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
              weapon,charged,seat,t,slot,b[10],b[22],b[23]&127,b[17],b[24],eagle_word(b+0x31));
          uint8_t projected[0x20000];memcpy(projected,g_ram,sizeof(projected));
          memcpy(projected+0x1228,c.players[seat].shots,sizeof(c.players[seat].shots));
          const MmxSpriteAsset *art=MmxRenderAssetsNativeShot(projected,0x1228+slot*64,b[22],b[23]&127,seat);
          if(!art)fprintf(stderr,"missing native shot art kind=%u group=%u pose=%u table=%x\n",b[10],b[22],b[23]&127,eagle_word(b+0x31));
          graphics&=art!=NULL;
          seen[seat]|=1u<<b[10];
        }
      }
      if(t==8 || t==226 || t==290) {
        char path[80];snprintf(path,sizeof(path),"rush-coop-weapons-%u-%u-%u.bmp",weapon,charged,t);
        rush_visual_bmp(path);
      }
    }
  }
  fclose(log);check(graphics,"every visible normal and charged special has private native graphics");
  check((seen[0]&0x00fd7f80)==0x00fd7f80 && (seen[1]&0x00fd7f80)==0x00fd7f80,
      "both controllers produce every visible normal and charged native special");
  puts("MMX BOSS RUSH CO-OP WEAPON CHECKS PASSED");
}
