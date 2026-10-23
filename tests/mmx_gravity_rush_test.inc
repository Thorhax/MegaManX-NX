/* Keep a real cast alive across fresh native boss allocation. Clearing the
 * encounter below is fixture setup, never evidence of a successful defeat. */
static void rush_gravity_frame(unsigned input) {
  /* The native hurt latch keeps live bosses from interrupting
   * the charge/cast. Do not change a boss's HP, AI or hit response. */
  g_ram[0xbd8]=120;
  frame(input);rush_keep_players_alive();
}
static void rush_gravity_clear(void) {
  MmxBossRushState s=MmxBossRushGetState();
  for(unsigned d=0xe68;d<0x1d08;) {
    if(d==0x1228) {d=0x1428;continue;}
    if(MmxBossRushOwner(d)>=0) memset(g_ram+d,0,d<0x1628?64:d<0x1928?48:32);
    d+=d<0x1628?64:d<0x1928?48:32;
  }
  memset(s.bosses,0,sizeof(s.bosses));memset(s.owner,0,sizeof(s.owner));
  s.defeated=0;s.queued=8;
  for(unsigned i=0;i<8;++i)s.queue[i]=(uint8_t)i;
  check(MmxBossRushSetState(&s),"gravity fixture clears encounter ownership");
}
static void rush_gravity_checks(uint8_t *start,uint8_t *field,uint8_t *actual,size_t cap) {
  const char *assets=getenv("MMX_WEAPONS_TEST_ASSETS");
  check(assets && MmxWeaponsLoad(assets),"rush gravity loads original weapon assets");
  rush_gravity_clear(); /* Use the same throwaway opening pair for both casts. */
  size_t baseline=RtlSaveSnapshotToMemory(start,cap);
  uint8_t *expected=malloc(cap);check(expected!=NULL,"gravity replay scratch allocates");
  unsigned failures=0;bool stale_checked=false;
  for(unsigned charged=0;charged<2;++charged) {
    check(RtlLoadSnapshotFromMemory(start,baseline),"restore arena before gravity cast");
    MmxWeaponsState w=MmxWeaponsGetState();w.page=2;w.weapon=6;MmxWeaponsSetState(w);
    /* Protect only the player: boss HP, states and success flags stay native. */
    /* Allow the throwaway opening pair's native intro/fire locks to finish
     * before the charge fills; they are discarded only after a real cast. */
    for(unsigned t=0;t<(charged?900:1);++t) rush_gravity_frame(SNES_PAD_Y);
    if(charged)fprintf(stderr,"gravity charge ready=%u arms=%u action=%u:%u flags=%u/%u/%u energy=%u\n",
        MmxWeaponsGetState().charge,g_ram[0x1f99],g_ram[0xbaa],g_ram[0xbab],g_ram[0xc01],g_ram[0xc25],g_ram[0x1f0c],MmxWeaponsEnergyAmount(2,6));
    rush_gravity_frame(0);
    for(unsigned t=0;t<(charged?100:65);++t) rush_gravity_frame(0);
    MmxWeaponCombatState combat=MmxWeaponsGetCombatState();bool formed=false;
    for(unsigned i=0;i<8;++i) formed|=combat.shots[i].active && combat.shots[i].page==2 &&
        combat.shots[i].weapon==6 && !combat.shots[i].variant && combat.shots[i].muzzle_pose==2;
    if(!formed)for(unsigned i=0;i<8;++i)fprintf(stderr,"gravity setup slot=%u active=%u charged=%u age=%u variant=%u phase=%u player=%u:%u\n",
        i,combat.shots[i].active,combat.shots[i].charged,combat.shots[i].age,combat.shots[i].variant,combat.shots[i].muzzle_pose,g_ram[0xbaa],g_ram[0xbab]);
    check(formed,"actual Y input forms a live gravity field before replacement");
    rush_gravity_clear();
    size_t n=RtlSaveSnapshotToMemory(field,cap);
    for(unsigned boss=0;boss<8;++boss) for(unsigned seat=0;seat<2;++seat) {
      check(RtlLoadSnapshotFromMemory(field,n),"restore active field for each boss and seat");
      MmxBossRushState s=MmxBossRushGetState();unsigned other=(boss+1)%8,pos=2;
      s.queue[seat]=(uint8_t)boss;s.queue[1-seat]=(uint8_t)other;
      for(unsigned i=0;i<8;++i)if(i!=boss && i!=other)s.queue[pos++]=(uint8_t)i;
      check(MmxBossRushSetState(&s),"gravity test selects native replacement queue");
      bool captured=false,complete=false;
      unsigned t;
      for(t=0;t<1500;++t) {
        frame(0);rush_keep_players_alive();s=MmxBossRushGetState();
        combat=MmxWeaponsGetCombatState();
        unsigned body=s.bosses[seat].object;
        if(!stale_checked && body && g_ram[body+1]==2 && (g_ram[body+0x27]&127)) {
          MmxWeaponShot *bad=combat.effects+15;memset(bad,0,sizeof(*bad));
          bad->active=1;bad->age=1;bad->page=2;bad->weapon=6;bad->variant=2;
          bad->origin_x=(int16_t)body;bad->origin_y=g_ram[body+10];
          bad->x=(int32_t)eagle_word(g_ram+body+5)*256;
          bad->y=(int32_t)eagle_word(g_ram+body+8)*256;
          MmxWeaponsSetCombatState(combat);
          size_t saved=RtlSaveSnapshotToMemory(actual,cap);
          check(RtlLoadSnapshotFromMemory(actual,saved),"legacy illegal gravity capture snapshot loads");
          combat=MmxWeaponsGetCombatState();
          check(!MmxWeaponsMovedEnemy(&combat,body),"legacy capture cannot suspend or replace a boss's native render/AI");
          unsigned hp=g_ram[body+0x27]&127;
          frame(0);rush_keep_players_alive();combat=MmxWeaponsGetCombatState();s=MmxBossRushGetState();
          check(!combat.effects[15].active && (g_ram[body+0x27]&127)>=hp && !s.defeated,
              "legacy capture retires without moving or dissolving the entering boss");
          stale_checked=true;
        }
        for(unsigned i=0;i<2;++i) if(s.bosses[i].object)
          captured|=MmxWeaponsMovedEnemy(&combat,s.bosses[i].object)!=NULL;
        if(captured || s.defeated) break;
        if(s.bosses[0].phase==MMX_RUSH_FIGHTING && s.bosses[1].phase==MMX_RUSH_FIGHTING &&
            s.bosses[0].health==32 && s.bosses[1].health==32) {complete=true;break;}
      }
      fprintf(stderr,"gravity-rush charged=%u boss=%u seat=%u ticks=%u captured=%u defeats=%u phases=%u/%u hp=%u/%u classes=%u/%u\n",
          charged,boss,seat,t,captured,s.defeated,s.bosses[0].phase,s.bosses[1].phase,
          s.bosses[0].health,s.bosses[1].health,g_ram[s.bosses[0].object+0x28],g_ram[s.bosses[1].object+0x28]);
      failures+=captured || s.defeated || !complete;
      if(captured) {
        /* Preserve the exploit's native consequence as well as its capture. */
        for(unsigned k=0;k<100;++k) {frame(0);rush_keep_players_alive();}
        fprintf(stderr,"gravity-rush consequence defeats=%u\n",MmxBossRushGetState().defeated);
      }
      if(!charged && !boss && !seat) {
        size_t replay=RtlSaveSnapshotToMemory(actual,cap);
        for(unsigned k=0;k<20;++k) {frame(0);rush_keep_players_alive();}
        size_t en=RtlSaveSnapshotToMemory(expected,cap);
        check(RtlLoadSnapshotFromMemory(actual,replay),"gravity/boss entrance snapshot restores");
        for(unsigned k=0;k<20;++k) {frame(0);rush_keep_players_alive();}
        size_t result=RtlSaveSnapshotToMemory(actual,cap);
        same(expected,en,actual,result,"gravity/boss entrance replays deterministically");
      }
    }
  }
  free(expected);
  check(stale_checked,"legacy-capture defense exercises a real boss entrance");
  check(!failures,"Gravity Well cannot capture or defeat entering bosses in either seat");
  puts("MMX BOSS RUSH GRAVITY CHECKS PASSED");
}
