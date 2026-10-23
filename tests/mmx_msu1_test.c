/* The title plugin and real PCM device, with no speaker/device output. */
#define _POSIX_C_SOURCE 200809L
#include "mod_runtime.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include "snes/msu1.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static SNESModActivationCallback activate, reset;
static SNESModFrameCallback tick;
static SNESModApuWriteCallback apu;
int snes_mod_register_activation_plugin(const char *id, SNESModActivationCallback cb) {
  (void)id; activate=cb; return 1;
}
int snes_mod_register_reset_callback(SNESModActivationCallback cb) {reset=cb;return 1;}
int snes_mod_register_frame_callback(SNESModFrameCallback cb) {tick=cb;return 1;}
int snes_mod_register_apu_write_callback(SNESModApuWriteCallback cb) {apu=cb;return 1;}
void interp_bridge_set_pre_opcode_hook(uint32_t pc,InterpPreOpcodeHook hook) {(void)pc;(void)hook;}
void interp_bridge_pre_opcode_redirect(uint32_t pc) {(void)pc;abort();}
uint8 cpu_read8(CpuState *cpu,uint8 bank,uint16 addr) {(void)cpu;(void)bank;(void)addr;abort();}
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}
double RtlAudioOutputRate(void) {return 44100;}
extern int mmx_msu1_music_command(uint8_t command);

static void pack(void) {
  FILE *f=fopen("msu-test-7.pcm","wb");assert(f);
  const uint8_t header[]={ 'M','S','U','1',0,0,0,0 };
  assert(fwrite(header,1,sizeof(header),f)==sizeof(header));
  for(unsigned i=0;i<4096;++i) {
    int16_t pair[2]={(int16_t)(1000+i),(int16_t)(-1000-(int)i)};
    assert(fwrite(pair,sizeof(pair),1,f)==1);
  }
  assert(!fclose(f));
}
static int sample(void) {
  int16_t out[2]={0};msu1_mix(out,1);assert(out[0]==-out[1]);return out[0];
}
int main(void) {
  assert(activate && reset);
  pack();
#ifdef _WIN32
  _putenv_s("SNESRECOMP_MSU1","msu-test");
#else
  setenv("SNESRECOMP_MSU1","msu-test",1);
#endif
  msu1_init();reset();
  assert(!mmx_msu1_music_command(0x17)); /* Disabled plugin stays stock. */
  activate();assert(tick && apu);
  assert(mmx_msu1_music_command(0x17));
  int before=0;for(unsigned i=0;i<64;++i)before=sample();assert(before>0);
  assert(!apu(0x2140,0xf6));
  for(unsigned i=0;i<100;++i)tick();
  assert(!(msu1_read(0x2000)&0x10) && sample()==0);
  assert(!apu(0x2140,0xf5));
  for(unsigned i=0;i<100;++i)tick();
  assert(msu1_read(0x2000)&0x10);
  assert(sample()>=before); /* Resume the selected PCM, retaining its cursor. */
  assert(!apu(0x2140,0xff));assert(sample()>0);
  assert(!apu(0x2140,0xfe));assert(sample()>=before);
  assert(!mmx_msu1_music_command(0x18)); /* Missing PCM uses SPC. */
  assert(!apu(0x2140,0xf5));assert(sample()==0);
  reset();assert(!mmx_msu1_music_command(0x17));
  assert(!remove("msu-test-7.pcm"));
  puts("MSU capsule fade/resume, pause, missing-track fallback and disabled mod passed");
  return 0;
}
