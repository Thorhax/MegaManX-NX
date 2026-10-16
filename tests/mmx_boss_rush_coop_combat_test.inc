/* Real-frame Boss Rush co-op combat: no synthetic HP subtraction or healing
 * during the measured contact/recovery and projectile windows. */
static void rush_coop_player(unsigned seat,unsigned x,unsigned y,unsigned weapon) {
  check(MmxCoopSelect(g_ram,seat),"combat fixture selects its player");
  MmxZeroCancel(g_ram);
  MmxWeaponsState w=MmxWeaponsGetState();w.page=w.weapon=0;MmxWeaponsSetState(w);
  memset(g_ram+0x1228,0,0x200);
  eagle_putword(g_ram+0xbad,x);eagle_putword(g_ram+0xbb0,y);
  g_ram[0xbac]=g_ram[0xbaf]=0;
  g_ram[0xbaa]=g_ram[0xbab]=0;
  g_ram[0xbcf]=0xa0;g_ram[0xc30]=g_ram[0xc32]=g_ram[0xbd8]=0;
  /* This is a fresh hit, not a continuation of entrance damage. An old
   * blink countdown would expire and clear the newly initialized hurt latch. */
  g_ram[0xc13]=0;
  g_ram[0xbdb]=(uint8_t)(weapon*2);g_ram[0xbb9]&=~64u;
  g_ram[0xbde]=g_ram[0xbe2]=g_ram[0xbe3]=0;
  MmxCoopCapture(g_ram);
}
static void rush_coop_targets(unsigned target,unsigned x,unsigned y) {
  MmxBossRushState s=MmxBossRushGetState();
  for(unsigned seat=0;seat<2;++seat) {
    unsigned d=s.bosses[seat].object;
    eagle_putword(g_ram+d+5,seat==target?x:s.camera_x-128);
    eagle_putword(g_ram+d+8,y);
  }
}
static void rush_coop_combat_checks(uint8_t *arena,uint8_t *pair,size_t cap) {
  check(MmxCoopEnabled(),"Boss Rush combat validation enables both player controllers");
  size_t empty=RtlSaveSnapshotToMemory(arena,cap);check(empty,"co-op combat arena saves");
  bool independent=true,recovery=true,weakness=true;
  FILE *log=fopen("rush-coop-combat.csv","w");check(log!=NULL,"co-op combat trace opens");
  fputs("case,boss_slot,player,frame,p1_hp,p2_hp,p1_action,p2_action,p1_iframes,p2_iframes,boss_hp,boss_state\n",log);
  for(unsigned encounter=0;encounter<16;++encounter) {
    unsigned boss=encounter/2,order=encounter%2;
    check(RtlLoadSnapshotFromMemory(arena,empty),"co-op combat empty arena restores");
    MmxBossRushState s=MmxBossRushGetState();
    memset(s.bosses,0,sizeof(s.bosses));memset(s.owner,0,sizeof(s.owner));
    memset(g_ram+0xe68,0,0x3c0);memset(g_ram+0x1428,0,0x8e0);
    s.queue[order]=(uint8_t)boss;s.queue[order^1]=boss?0:5;s.queued=8;
    unsigned q=2;for(unsigned i=0;i<8;++i)if(i!=s.queue[0] && i!=s.queue[1])s.queue[q++]=(uint8_t)i;
    check(MmxBossRushSetState(&s),"co-op combat selects each Maverick in both slots");
    for(unsigned tick=0;tick<1500;++tick) {
      frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
      if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING)break;
    }
    check(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING,
        "co-op combat completes both native introductions");
    size_t saved=RtlSaveSnapshotToMemory(pair,cap);check(saved,"co-op combat pair saves");
    unsigned x=s.camera_x+176,y=s.camera_y+164;
    for(unsigned player=0;player<2;++player) {
      check(RtlLoadSnapshotFromMemory(pair,saved),"isolated boss contact restores");
      rush_coop_player(player^1,s.camera_x+48,y,0);
      rush_coop_player(player,x,y,0);MmxCoopSelect(g_ram,0);
      unsigned target=order,body=s.bosses[target].object;
      bool hurt=false,immune=false,moved=false,blink=false,shown=false;
      unsigned damaged_hp=32,hit_tick=0;
      for(unsigned tick=0;tick<100;++tick) {
        /* Remove projectiles so only this boss body's contact is measured. */
        memset(g_ram+0x1428,0,0x8e0);
        /* Leave the body at the contact position while native knockback
         * moves its victim away. Never chase the victim with a teleported
         * boss: that changes Octopus's native grab/current interaction. */
        rush_coop_targets(target,!hurt || tick<hit_tick+16?x:s.camera_x+320,y);
        frame(0);MmxCoopState c=MmxCoopGetState();
        const uint8_t *p=c.players[player].body;
        unsigned hp=p[0x27]&127;
        fprintf(log,"contact,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",target,player,tick,
            c.players[0].body[0x27]&127,c.players[1].body[0x27]&127,
            c.players[0].body[2],c.players[1].body[2],c.players[0].body[0x8a],c.players[1].body[0x8a],
            g_ram[body+0x27]&127,g_ram[body+1]);
        independent&=(c.players[player^1].body[0x27]&127)==32;
        if(hp<32 && !hurt) {
          hurt=true;damaged_hp=hp;hit_tick=tick;
          fprintf(stderr,"rush contact boss=%u boss_slot=%u player=%u character=%u hp=%u/%u action=%u:%u immune=%u visible=%u\n",
              boss,target,player,c.players[player].character,c.players[0].body[0x27]&127,c.players[1].body[0x27]&127,
              p[2],p[3],p[0x8a],p[14]);
        }
        if(hurt) {
          immune|=p[0x8a]!=0;blink|=p[14]==0;shown|=p[14]!=0;
          moved|=abs((int)eagle_word(p+5)-(int)x)>3;
          recovery&=hp==damaged_hp;
          if(tick==hit_tick+1 || tick==hit_tick+8) {
            char path[80];snprintf(path,sizeof(path),"rush-coop-hurt-%u-%u-%u.bmp",encounter,player,tick-hit_tick);
            rush_visual_bmp(path);
          }
        }
      }
      independent&=hurt;
      fprintf(stderr,"rush recovery boss=%u boss_slot=%u player=%u hurt=%u immune=%u knockback=%u blink=%u/%u\n",
          boss,target,player,hurt,immune,moved,blink,shown);
      recovery&=immune && moved && blink && shown;
    }
    if(boss!=5)continue;
    unsigned damage[2][2]={{0}};
    for(unsigned player=0;player<2;++player)for(unsigned special=0;special<2;++special) {
      check(RtlLoadSnapshotFromMemory(pair,saved),"Chameleon projectile fixture restores");
      rush_coop_player(player^1,s.camera_x+32,y,0);
      rush_coop_player(player,s.camera_x+100,y,special?7:0);MmxCoopSelect(g_ram,0);
      unsigned target=order,body=s.bosses[target].object;
      g_ram[body+0x27]=32;g_ram[body+0x35]=0;
      bool spawned=false;
      for(unsigned tick=0;tick<100;++tick) {
        rush_coop_targets(target,x,y);
        /* Isolate projectile weakness from contact/knockback. */
        g_ram[s.bosses[0].object+0x26]=g_ram[s.bosses[1].object+0x26]=0;
        MmxCoopSelect(g_ram,player);
        for(unsigned d=0x1228;d<0x1428;d+=64)if(g_ram[d]) {
          spawned=true;memcpy(g_ram+d+5,g_ram+body+5,2);memcpy(g_ram+d+8,g_ram+body+8,2);
        }
        MmxCoopSelect(g_ram,0);
        frame(tick?0:SNES_PAD_Y<<(12*player));
        MmxCoopState c=MmxCoopGetState();
        fprintf(log,"weapon%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",special,target,player,tick,
            c.players[0].body[0x27]&127,c.players[1].body[0x27]&127,
            c.players[0].body[2],c.players[1].body[2],c.players[0].body[0x88],c.players[1].body[0x88],
            g_ram[body+0x27]&127,g_ram[body+1]);
        unsigned hp=g_ram[body+0x27]&127;
        if(hp<32) {damage[player][special]=32-hp;break;}
      }
      fprintf(stderr,"rush Chameleon boss_slot=%u player=%u weapon=%u spawned=%u damage=%u\n",
          target,player,special?7:0,spawned,damage[player][special]);
      weakness&=spawned && damage[player][special]>0;
    }
    weakness&=damage[0][0]==damage[1][0] && damage[0][1]==damage[1][1] && damage[0][1]>damage[0][0];
  }
  fclose(log);
  check(independent,"native boss contact damages only the overlapping co-op player in either slot");
  check(recovery,"both co-op players retain native invulnerability, knockback and visibility flicker");
  check(weakness,"P1 and P2 Boomerang Cutter trigger the same native Chameleon weakness in both boss slots");
  puts("MMX BOSS RUSH CO-OP COMBAT CHECKS PASSED");
}
