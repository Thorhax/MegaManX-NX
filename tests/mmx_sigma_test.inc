/* Private ROM-backed replay of the final Sigma intro. Use desktop timing:
 * legacy frame-counter timing hides a missing restored APU clock. */
static void sigma_checks(const char *fixture, uint8_t *start,
                         uint8_t *expected, uint8_t *actual, size_t cap) {
  RtlEnableExtendedFrameTiming();
  check(MmxCoopEnable(MMX_COOP_X), "Sigma co-op enables");
  check(RtlLoadSnapshot(fixture), "Sigma legacy checkpoint loads");
  unsigned boss=coop_vile_slot(0x65);
  check(g_ram[0x1f7a]==12 && boss && g_ram[boss+1]==12,
        "fixture contains the final Sigma intro");
  MmxCoopState s=MmxCoopGetState();
  bool partner=s.players[1].status==MMX_COOP_ALIVE, ready=false;
  uint64_t clock=g_snes->apu->portClock;
  for(unsigned tick=0;tick<1800;++tick) {
    unsigned pad=g_ram[0x1f3b] && tick%30==0 ? SNES_PAD_A|SNES_PAD_B : 0;
    coop_frames(1,pad,pad);s=MmxCoopGetState();
    if(g_ram[boss+1]==16 && !g_ram[0xc16] && !s.scene_owner &&
        s.players[0].body[2]<4 && (!partner || (s.players[1].status==MMX_COOP_ALIVE &&
        s.players[1].body[2]<4 && !s.players[1].zero.swap_phase))) {ready=true;break;}
  }
  check(ready && g_snes->apu->portClock>clock,
        "Sigma music handoff completes and the fight becomes playable");
  unsigned x0=coop_x(&s.players[0]),x1=coop_x(&s.players[1]);
  coop_frames(8,SNES_PAD_RIGHT,SNES_PAD_RIGHT);s=MmxCoopGetState();
  check(coop_x(&s.players[0])>x0 && (!partner || coop_x(&s.players[1])>x1),
        "each active player regains movement after the final intro");

  size_t n=RtlSaveSnapshotToMemory(start,cap);
  check(n && RtlSaveSnapshot("sigma-playable.sav"), "playable Sigma checkpoint saves");
  coop_frames(20,0,0);
  size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,n), "Sigma memory snapshot restores");
  coop_frames(20,0,0);
  size_t an=RtlSaveSnapshotToMemory(actual,cap);
  same(expected,en,actual,an,"Sigma save/load reproduces gameplay and SPC state");
  check(RtlLoadSnapshot("sigma-playable.sav"), "Sigma file snapshot restores");
  coop_frames(20,0,0);an=RtlSaveSnapshotToMemory(actual,cap);
  same(expected,en,actual,an,"Sigma file load preserves the carried audio clock");

  n=RtlRollbackSaveToMemory(start,cap);
  check(n!=0,"Sigma rollback checkpoint saves");
  coop_frames(20,0,0);en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlRollbackLoadFromMemory(start,n), "Sigma rollback checkpoint restores");
  coop_frames(20,0,0);an=RtlSaveSnapshotToMemory(actual,cap);
  same(expected,en,actual,an,"Sigma rollback reproduces gameplay and SPC state");
  MmxCoopDisable();puts("MMX SIGMA CHECKS PASSED");
}
