#include "mmx_hadouken.h"
extern RecompReturn bank_81_A062_M1X1(CpuState *cpu);
static void hadouken_checks(const char *fixture,uint8_t *start,uint8_t *expected,uint8_t *actual,size_t cap) {
  MmxHadoukenSetEnabled(true);
  for(unsigned roster=0;roster<2;++roster) for(unsigned scenario=0;scenario<8;++scenario) {
    check(MmxCoopEnable(roster) && RtlLoadSnapshot(fixture),"Hadouken checkpoint loads");
    coop_frames(8,0,0);
    MmxCoopState s=MmxCoopGetState();
    unsigned seat=roster?1:0;
    if(seat && s.players[seat].status!=MMX_COOP_ALIVE) {
      check(coop_place_fixture_partner(coop_x(&s.players[0])+24,coop_y(&s.players[0])),"Hadouken X partner joins");
      coop_frames(24,0,0);s=MmxCoopGetState();
    }
    s.current=s.anchor=0;
    s.players[seat].character=MMX_COOP_X;s.players[seat^1].character=MMX_COOP_ZERO;
    MmxCoopPlayer *p=&s.players[seat];
    p->status=MMX_COOP_ALIVE;p->zero.swap_phase=0;p->zero.active_x=1;
    p->body[1]=2;p->body[2]=p->body[3]=0;p->body[0x27]=scenario==2?31:32;
    p->body[0x33]=scenario==3?2:0;p->body[0x7d]=p->body[0x8d]=0;
    p->weapons.page=p->weapons.weapon=scenario==6?1:0;
    if(scenario==7) p->body[2]=6;
    p->body[0x7f]=p->body[0x80]=0;
    if(scenario==4) {p->character=MMX_COOP_ZERO;s.players[seat^1].character=MMX_COOP_X;}
    for(unsigned i=0;i<2;++i) s.players[i].zero.active_x=s.players[i].character==MMX_COOP_X;
    check(MmxCoopValidState(&s),"Hadouken roster remains valid");
    g_ram[0x1f7e]=scenario==1?0:128;g_ram[0x1f9a]=32;
    memcpy(g_ram+0xba8,s.players[0].body,sizeof(s.players[0].body));
    MmxZeroSetState(s.players[0].zero);MmxWeaponsSetState(s.players[0].weapons);
    MmxCoopSetState(&s);
    MmxHadoukenSetEnabled(scenario!=0 || roster!=0);
    check(MmxCoopSelect(g_ram,seat),"Hadouken player context projects");
    MmxCoopPoll(seat?0:SNES_PAD_Y,seat?SNES_PAD_Y:0);MmxCoopApplyInput(g_ram);
    CpuState probe;cpu_state_init(&probe,g_ram);probe.D=0xba8;probe.PB=probe.DB=0x81;
    check(bank_81_A062_M1X1(&probe)==RECOMP_RETURN_NORMAL,"native Hadouken recognizer runs");
    MmxCoopCapture(g_ram);s=MmxCoopGetState();
    check((s.players[seat].body[2]==0x42)==(scenario==5 || (scenario==0 && roster==1)),"Hadouken retains native eligibility and defaults");
  }
  check(RtlLoadSnapshot(fixture),"Hadouken repeat checkpoint loads");
  MmxKncBugfixReset();
  uint8_t r[0x20000]={0};r[0xd1]=2;r[0xd2]=r[0xd3]=4;
  MmxCoopDisable();MmxZeroDisable();
  const uint16_t sequence[]={16,16,32,32,64,128,64,128,1,256,8};
  for(unsigned i=0;i<11;++i) {MmxKncBugfixTick(r,sequence[i],0);MmxKncBugfixTick(r,0,0);}
  check(!MmxKncBugfixActive(0),"KNC Bugfix");
  r[0xc3]=128;r[0x1f10]=6;MmxKncBugfixTick(r,0,0);
  r[0xc3]=0;r[0x1f10]=0;MmxKncBugfixTick(r,0,0);
  check(MmxKncBugfixActive(0) && !r[0xc31] && !r[0xc38],"KNC Bugfix");
  size_t n=RtlSaveSnapshotToMemory(start,cap);check(n>0,"KNC Bugfix");
  MmxKncBugfixReset();check(RtlLoadSnapshotFromMemory(start,n),"KNC Bugfix");
  check(MmxKncBugfixActive(0),"KNC Bugfix");
  MmxWeaponsDisable();g_config.widescreen=false;
  int knc_bugfix_w,knc_bugfix_h;MmxPrepareFrame(1280,720,&knc_bugfix_w,&knc_bugfix_h);
  check(g_mmx_custom_renderer && knc_bugfix_w==256,"KNC Bugfix");
  coop_frames(8,0,0);
  static uint32_t knc_bugfix_a[256*224],knc_bugfix_b[256*224];
  uint8_t knc_bugfix_ram[0x20000];memcpy(knc_bugfix_ram,g_ram,sizeof(knc_bugfix_ram));
  MmxKncBugfixState knc_bugfix=MmxKncBugfixGetState();
  MmxRendererReset();MmxRendererHoldFrame(true);
  MmxRendererBeginFrame(g_ram);MmxRendererCoopFrame(NULL);
  for(unsigned line=1;line<=224;++line) MmxRendererCaptureLine(g_ppu,line);
  static const uint32_t knc_bugfix_blank[256*224]={0};
  check(MmxRendererEndFrame(knc_bugfix_blank) &&
      MmxRendererDraw(knc_bugfix_a,(MmxRenderView){256,0,4.0/3.0},false),"KNC Bugfix");
  MmxKncBugfixReset();
  check(MmxRendererDraw(knc_bugfix_b,(MmxRenderView){256,0,4.0/3.0},false),"KNC Bugfix");
  unsigned knc_bugfix_changes=0;
  for(unsigned i=0;i<256*224;++i) knc_bugfix_changes+=knc_bugfix_a[i]!=knc_bugfix_b[i];
  check(knc_bugfix_changes>0 && !memcmp(knc_bugfix_ram,g_ram,sizeof(knc_bugfix_ram)),"KNC Bugfix");
  MmxKncBugfixSetState(knc_bugfix);MmxRendererHoldFrame(false);
  (void)expected;(void)actual;
  puts("MMX HADOUKEN CHECKS PASSED");
}
