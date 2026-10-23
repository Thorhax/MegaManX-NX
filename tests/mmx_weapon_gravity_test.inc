static unsigned gravity_test_slot(MmxWeaponCombatState c,unsigned variant) {
  for(unsigned i=0;i<8;++i) if(c.shots[i].active && c.shots[i].page==2 &&
      c.shots[i].weapon==6 && c.shots[i].variant==variant) return i;
  return 8;
}
static void weapon_gravity_checks(const char *assets,const char *fixture,uint8 *start,
                                 uint8 *expected,uint8 *actual,size_t cap) {
  check(MmxWeaponsLoad(assets),"Gravity Well original graphics load");
  for(unsigned character=0;character<2;++character) {
    check(RtlLoadSnapshot(fixture),"restore Gravity Well fixture");if(character) zero_health_swap();
    MmxWeaponsState w=MmxWeaponsGetState();w.page=2;w.weapon=6;MmxWeaponsSetState(w);
    frame(SNES_PAD_Y);frame(0);MmxWeaponCombatState c=MmxWeaponsGetCombatState();unsigned slot=gravity_test_slot(c,0);
    check(slot<8 && c.shots[slot].vx==992 && MmxWeaponsEnergyAmount(2,6)==27*256,
        "Gravity Well follows source 4-to-3.875 speed and costs one energy");
    zero_replay(55);c=MmxWeaponsGetCombatState();
    check(c.shots[slot].active && c.shots[slot].muzzle_pose==2 && c.shots[slot].radius>150,
        "normal orb decelerates, forms and sustains the source well");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-gravity-well.cap":".zero-gravity-well.cap");
    size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(220);size_t en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore normal Gravity Well");zero_replay(220);
    size_t an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"well duration and homing return replay exactly");
    c=MmxWeaponsGetCombatState();check(gravity_test_slot(c,0)==8 && !g_ram[0xbdd],"well returns and releases its firing slot");
    for(unsigned i=0;i<330;++i) frame(SNES_PAD_Y);frame(0);zero_replay(3);
    check(gravity_test_slot(MmxWeaponsGetCombatState(),1)==8,"Gravity Well charge requires X1 arms");
    MmxWeaponsCancelShots(g_ram);zero_replay(90);MmxWeaponsRefill();g_ram[0x1f99]|=2;
    for(unsigned i=0;i<330;++i) frame(SNES_PAD_Y);frame(0);zero_replay(12);
    c=MmxWeaponsGetCombatState();unsigned body=gravity_test_slot(c,1);slot=gravity_test_slot(c,0);
    check(body<8 && slot<8 && c.shots[body].group==51 && MmxWeaponsEnergyAmount(2,6)==26*256,
        "charged Gravity Well uses original ground cast and one additional energy");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-gravity-cast.cap":".zero-gravity-cast.cap");
    unsigned x=g_ram[0xbad]|g_ram[0xbae]<<8;
    for(unsigned i=0;i<100;++i) frame(SNES_PAD_RIGHT);
    c=MmxWeaponsGetCombatState();check(c.shots[slot].muzzle_pose==2 &&
        (unsigned)(g_ram[0xbad]|g_ram[0xbae]<<8)==x,"source charged field locks the cast and launches its orb upward");
    bool particles=false;for(unsigned i=0;i<16;++i) particles|=c.effects[i].active && c.effects[i].variant==3;
    check(particles,"charged field emits original upward particles");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-gravity-field.cap":".zero-gravity-field.cap");
    n=RtlSaveSnapshotToMemory(start,cap);zero_replay(310);en=RtlSaveSnapshotToMemory(expected,cap);
    check(RtlLoadSnapshotFromMemory(start,n),"restore charged Gravity Well");zero_replay(310);
    an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"charged field and recovery replay exactly");
    check(gravity_test_slot(MmxWeaponsGetCombatState(),1)==8 && !g_ram[0xbdd],"charged field expires and ends the casting lock");
    x=g_ram[0xbad]|g_ram[0xbae]<<8;for(unsigned i=0;i<12;++i) frame(SNES_PAD_RIGHT);
    check((unsigned)(g_ram[0xbad]|g_ram[0xbae]<<8)>x,"player movement resumes after the field");
    MmxWeaponsCancelShots(g_ram);MmxWeaponsRefill();
    for(unsigned i=0;i<330;++i) frame(SNES_PAD_Y);
    unsigned py=(g_ram[0xbb0]|g_ram[0xbb1]<<8)-64;
    g_ram[0xbb0]=(uint8_t)py;g_ram[0xbb1]=(uint8_t)(py>>8);g_ram[0xbd3]&=(uint8_t)~4;
    g_ram[0xbaa]=8;g_ram[0xbab]=2;g_ram[0xbc4]=g_ram[0xbc5]=0;
    frame(0);zero_replay(4);c=MmxWeaponsGetCombatState();body=gravity_test_slot(c,1);
    check(body<8 && c.shots[body].group==52,"airborne charge selects the original air cast");
    zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),character?".x-gravity-air.cap":".zero-gravity-air.cap");
  }
  check(RtlLoadSnapshot(fixture),"restore real enemy Gravity Well encounter");unsigned victim=0;
  for(unsigned i=0;i<400 && !victim;++i) {
    frame(SNES_PAD_RIGHT);
    for(unsigned d=0xe68;d<0x1228;d+=64) if(g_ram[d] && g_ram[d+1]==2 &&
        (g_ram[d+0x27]&127) && g_ram[d+0x28]<6 && (g_ram[d+0x20]|g_ram[d+0x21])) {victim=d;break;}
  }
  check(victim!=0,"real native enemy found for gravity interaction");
  /* Ownership alone also tags ordinary minions; immunity belongs to the
   * boss body, not every actor spawned by it. Keep the real enemy mechanics. */
  MmxBossRushSetOwner(victim,0);
  uint8_t enemy[64];memcpy(enemy,g_ram+victim,64);
  MmxWeaponsState w=MmxWeaponsGetState();w.page=2;w.weapon=6;MmxWeaponsSetState(w);
  frame(0);frame(SNES_PAD_Y);frame(0);zero_replay(60);
  MmxWeaponCombatState c=MmxWeaponsGetCombatState();const MmxWeaponShot *p=MmxWeaponsMovedEnemy(&c,victim);
  check(p!=NULL,"formed well still captures an ordinary enemy with a boss ownership tag");
  unsigned px=g_ram[victim+5]|g_ram[victim+6]<<8,py=g_ram[victim+8]|g_ram[victim+9]<<8;
  uint8_t action[3];memcpy(action,g_ram+victim+1,3);zero_replay(10);
  check(!memcmp(action,g_ram+victim+1,3) && ((unsigned)(g_ram[victim+5]|g_ram[victim+6]<<8)!=px ||
      (unsigned)(g_ram[victim+8]|g_ram[victim+9]<<8)!=py),"gravity moves enemy while suspending its own AI");
  zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),".gravity-victim.cap");
  size_t n=RtlSaveSnapshotToMemory(start,cap);zero_replay(60);size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"restore pulled enemy");zero_replay(60);
  size_t an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"enemy pull and native death replay exactly");
  check(!g_ram[victim] || g_ram[victim+1]==4,"gravity dissolve enters native enemy death");
  check(RtlLoadSnapshot(fixture),"restore charged enemy lift encounter");
  w=MmxWeaponsGetState();w.page=2;w.weapon=6;MmxWeaponsSetState(w);g_ram[0x1f99]|=2;
  for(unsigned i=0;i<330;++i) frame(SNES_PAD_Y);frame(0);zero_replay(90);
  memcpy(g_ram+victim,enemy,64);
  MmxBossRushSetOwner(victim,1);
  unsigned ex=(g_ram[0x1e4d]|g_ram[0x1e4e]<<8)+170,ey=(g_ram[0x1e50]|g_ram[0x1e51]<<8)+130;
  g_ram[victim+5]=(uint8_t)ex;g_ram[victim+6]=(uint8_t)(ex>>8);
  g_ram[victim+8]=(uint8_t)ey;g_ram[victim+9]=(uint8_t)(ey>>8);g_ram[victim+0x28]=6;
  frame(0);c=MmxWeaponsGetCombatState();check(!MmxWeaponsMovedEnemy(&c,victim),"gravity excludes protected damage categories");
  g_ram[victim+0x28]=enemy[0x28];frame(0);c=MmxWeaponsGetCombatState();p=MmxWeaponsMovedEnemy(&c,victim);
  check(p && p->charged,"charged field captures an eligible native enemy");
  ey=g_ram[victim+8]|g_ram[victim+9]<<8;zero_replay(8);
  check((unsigned)(g_ram[victim+8]|g_ram[victim+9]<<8)<ey,"charged gravity accelerates the enemy upward");
  zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),".gravity-lift.cap");
  n=RtlSaveSnapshotToMemory(start,cap);zero_replay(60);en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n),"restore lifted enemy");zero_replay(60);
  an=RtlSaveSnapshotToMemory(actual,cap);same(expected,en,actual,an,"enemy lift and offscreen death replay exactly");
  check(!g_ram[victim] || g_ram[victim+1]==4,"lifted enemy enters native death above the screen");
  puts("MMX GRAVITY WELL CHECKS PASSED");
}
