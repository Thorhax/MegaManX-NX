/* A native boss defeat can overlap the weapon-menu coroutine's early camera
 * clear, before its HUD/menu flags become visible. Never spawn at menu X=0.
 * HP positioning triggers the real death controller; no defeat flags are set. */
static void rush_pause_spawn_checks(uint8_t *arena,uint8_t *combat,size_t cap) {
  size_t arena_size=RtlSaveSnapshotToMemory(arena,cap);
  check(arena_size,"pause-spawn arena saves");
  rush_report_pair(2,7);
  size_t combat_size=RtlSaveSnapshotToMemory(combat,cap);
  check(combat_size,"pause-spawn native combat saves");
  unsigned cases=0;
  for(unsigned seat=0;seat<2;++seat)for(unsigned delay=0;delay<90;++delay) {
    check(RtlLoadSnapshotFromMemory(combat,combat_size),"pause-spawn native combat restores");
    MmxBossRushState s=MmxBossRushGetState();unsigned generation=s.bosses[seat].generation;
    uint32_t defeated=s.defeated;
    s.queue[0]=1;unsigned q=1;
    for(unsigned i=0;i<8;++i)if(i!=1)s.queue[q++]=(uint8_t)i;
    s.queued=8;check(MmxBossRushSetState(&s),"pause-spawn queues a Mandrill replacement");
    g_ram[s.bosses[seat].object+0x27]=0;
    bool spawned=false,fighting=false;
    for(unsigned t=0;t<delay+1020;++t) {
      frame(t==delay || t==delay+120?SNES_PAD_START:0);rush_keep_players_alive();
      s=MmxBossRushGetState();
      if(s.bosses[seat].generation==generation)continue;
      unsigned d=s.bosses[seat].object,x=eagle_word(g_ram+d+5);
      if(!spawned) {
        /* The guest may open the menu later in this same frame. The actor
         * must retain arena coordinates even when the final camera is zero. */
        if(x<s.camera_x+32 || x>s.camera_x+336) {
          fprintf(stderr,"pause-spawn seat=%u delay=%u tick=%u x=%04x camera=%04x flags=%u/%u\n",
              seat,delay,t,x,eagle_word(g_ram+0x1e4d),g_ram[0xd3],g_ram[0x1f19]);
          check(false,"replacement enters inside the gameplay arena");
        }
        check(s.defeated==defeated+1 && s.bosses[seat].id==1,
            "native death awards one defeat and starts the queued Mandrill");
        spawned=true;
      }
      if(t>delay+120 && s.bosses[seat].phase==MMX_RUSH_FIGHTING) {fighting=true;break;}
    }
    check(spawned && fighting,"replacement completes its native entrance after pause resumes");
    ++cases;
  }
  check(RtlLoadSnapshotFromMemory(arena,arena_size),"pause-spawn restores its caller arena");
  printf("MMX BOSS RUSH PAUSE SPAWN PASSED: %u native timing/seat cases\n",cases);
}
