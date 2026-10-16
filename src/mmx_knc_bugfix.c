#include "mmx_knc_bugfix.h"
#include "mmx_coop.h"

/* KNC Bugfix */
static MmxKncBugfixState state;
void MmxKncBugfixReset(void) { memset(&state,0,sizeof(state)); }
MmxKncBugfixState MmxKncBugfixGetState(void) { return state; }
bool MmxKncBugfixValidState(const MmxKncBugfixState *s) {
  return s && s->progress[0]<11 && s->progress[1]<11 &&
      s->pending[0]<3 && s->pending[1]<3 && s->active[0]<2 && s->active[1]<2;
}
void MmxKncBugfixSetState(MmxKncBugfixState s) {
  if(MmxKncBugfixValidState(&s)) state=s;
  else MmxKncBugfixReset();
}
bool MmxKncBugfixActive(unsigned seat) { return seat<2 && state.active[seat]; }
unsigned MmxKncBugfixPhase(void) { return ((state.clock>>2)&7)*2; }
void MmxKncBugfixTick(const uint8_t *r,uint16_t p1,uint16_t p2) {
  static const uint16_t sequence[]={16,16,32,32,64,128,64,128,1,256,8};
  const uint16_t input[2]={p1&4095,p2&4095};
  MmxCoopState coop=MmxCoopGetState();
  bool in_stage=r[0xd1]==2 && r[0xd2]==4;
  bool menu=(r[0xc3]&128) && (r[0x1f10]==6 || r[0x1f10]==8);
  ++state.clock;
  for(unsigned seat=0;seat<2;++seat) {
    unsigned pressed=input[seat]&~state.previous[seat];
    state.previous[seat]=input[seat];
    bool x=coop.initialized ? coop.players[seat].character==MMX_COOP_X &&
        coop.players[seat].status==MMX_COOP_ALIVE : !seat && !MmxZeroActive();
    if(!in_stage || !x) {state.progress[seat]=state.pending[seat]=0;continue;}
    if(state.pending[seat]) {
      if(menu) state.pending[seat]=2;
      else if(state.pending[seat]==2 && r[0xd3]==4) {
        state.active[seat]=1;state.pending[seat]=0;
      }
      continue;
    }
    if(menu || r[0xd3]!=4 || !pressed) continue;
    unsigned n=state.progress[seat];
    if(pressed==sequence[n]) {
      if(++n==11) {state.pending[seat]=1;n=0;}
    } else n=pressed==sequence[0]?1:0;
    state.progress[seat]=(uint8_t)n;
  }
}
