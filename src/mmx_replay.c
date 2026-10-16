#include "mmx_replay.h"
#include "mmx_coop.h"
#include "mmx_coop_view.h"
#include "mmx_coop_trace.h"
#include "mmx_renderer.h"
#include "mmx_zero.h"
#include "mmx_weapons.h"
#if !MMX_VARIANT_JP
#include "saber/mmx_saber_plugin.h"
#endif
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "desktop/config.h"
#include "host_paths.h"
#include "mod_runtime.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include "netplay/snes_netplay.h"
#include "snes_runahead.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define replay_mkdir(p) _mkdir(p)
#define replay_pid() _getpid()
#else
#include <sys/stat.h>
#include <unistd.h>
#define replay_mkdir(p) mkdir(p, 0755)
#define replay_pid() getpid()
#endif
#ifndef MMX_SOURCE_REVISION
#define MMX_SOURCE_REVISION "dev"
#endif
#ifndef MMX_REPLAY_REVISION
#define MMX_REPLAY_REVISION MMX_SOURCE_REVISION
#endif
#ifndef MMX_REPLAY_ENGINE
#define MMX_REPLAY_ENGINE "unknown"
#endif
#ifndef SNESRECOMP_BUILD_VERSION
#define SNESRECOMP_BUILD_VERSION "dev"
#endif

/* Keep execution order, including corrections after a netplay rollback.
 * A discontinuity carries the actual restored machine, never a guessed
 * reconstruction from the last input for a tick. Cap disk use per session. */
enum { SEGMENT_FRAMES = 600, SEGMENTS = 6, STATE_LIMIT = 16*1024*1024,
       SNAPSHOT_LIMIT = 2*1024*1024 };
static bool enabled, playing, finished, failed, pending;
static FILE *stream, *states;
static char stem[1200], path[1280];
static char config_path[1200]="config.ini";
static unsigned segment, rows, next_frame;
static uint64_t generation;
static uint64_t load_sequence, recorded_load_sequence;
static uint32_t expected_wram, expected_coop, expected_views, expected_vram;
static bool playback_loaded;
static bool playback_checkpointed;
static unsigned playback_rows;
static int playback_width;
static void *snapshot;
static size_t snapshot_cap;

static unsigned mods(void) {
  unsigned m=MmxZeroEnabled() | MmxCoopEnabled()<<1 | MmxZeroModern()<<2 |
      MmxWeaponsPageEnabled(1)<<3 | MmxWeaponsPageEnabled(2)<<4;
#if !MMX_VARIANT_JP
  m |= MmxSaberEnabled()<<5;
#endif
  return m;
}
/* These settings may change mid-session. Record them at the same boundary as
 * the inputs; the viewport controls native spawn/cull margins as well as art. */
static unsigned policy(void) {
  return g_config.widescreen | g_mmx_custom_renderer<<1 | g_config.new_renderer<<2 |
      g_config.no_sprite_limits<<3 | MmxCoopViewsIndependent()<<4 |
      rtl_apu_extended_frame_timing()<<5 | MmxCoopViewsOnline()<<6;
}
void MmxReplayViewport(int *width,int *extra) {
  if(playing && playback_loaded) { *width=playback_width; *extra=(playback_width-256)/2; }
}

static uint32_t hash(const void *data, size_t size) {
  const uint8_t *b=data; uint32_t h=2166136261u;
  while(size--) h=(h^*b++)*16777619u;
  return h;
}
static void hashes(uint32_t *w, uint32_t *c, uint32_t *v, uint32_t *p) {
  MmxCoopState coop=MmxCoopGetState();
  MmxCoopViewWorldState views=MmxCoopViewsGetWorldState();
  *w=hash(g_ram,0x20000); *c=hash(&coop,sizeof(coop)); *v=hash(&views,sizeof(views));
  *p=hash(g_ppu->vram,sizeof(g_ppu->vram));
}
static void error(const char *why) {
  fprintf(stderr,"[mmx-replay] %s: %s\n",path,why);
  failed=true; pending=false;
  if(stream) { fclose(stream); stream=NULL; }
  if(states) { fclose(states); states=NULL; }
}
void MmxReplayClose(void) {
  if(stream) { if(fclose(stream)) failed=true; stream=NULL; }
  if(states) { if(fclose(states)) failed=true; states=NULL; }
  pending=false; rows=0;
}
void MmxReplayEnable(bool on) {
  if(on && !enabled && !playing) failed=false;
  enabled=on; if(!on && !playing) MmxReplayClose();
}
void MmxReplaySetConfigPath(const char *name) {
  if(!snesrecomp_abspath(name,config_path,sizeof(config_path)))
    fprintf(stderr,"[mmx-replay] cannot resolve configuration path\n");
}
/* RtlStateGeneration deliberately ignores rollback. Observe the title's load
 * callback too, including corrections restored at the same frame number. */
void MmxReplayStateLoaded(void) { ++load_sequence; }
bool MmxReplayPlaying(void) { return playing; }
bool MmxReplayFinished(void) { return finished; }
bool MmxReplayFailed(void) { return failed; }
const char *MmxReplayPath(void) { return path; }
static bool copy(const char *from,const char *to) {
  FILE *in=fopen(from,"rb"), *out=in?fopen(to,"wb"):NULL;
  bool ok=in && out; char buffer[4096]; size_t n;
  while(ok && (n=fread(buffer,1,sizeof(buffer),in))) ok=fwrite(buffer,1,n,out)==n;
  if(in) { if(ferror(in)) ok=false; fclose(in); }
  if(out && fclose(out)) ok=false;
  return ok;
}
static bool open_directory(const char *folder) {
  replay_mkdir(folder);
  char probe[1200]; snprintf(probe,sizeof(probe),"%s/.mmx-replay-%ld",folder,(long)replay_pid());
  FILE *f=fopen(probe,"wb"); if(!f) return false;
  fclose(f); remove(probe); return true;
}
static bool init_stem(void) {
  if(stem[0]) return true;
  char folder[1024],stamp[32];
  const char *override=getenv("MMX_REPLAY_DIR");
  if(override && *override) {
    if(snprintf(folder,sizeof(folder),"%s",override)>=(int)sizeof(folder) || !open_directory(folder)) return false;
  } else if(!(snesrecomp_exe_dir_path("logs",folder,sizeof(folder)) && open_directory(folder))) {
    const char *tmp=getenv("TEMP"); if(!tmp) tmp=getenv("TMPDIR"); if(!tmp) tmp=".";
    snprintf(folder,sizeof(folder),"%s/MegaManXSNESRecomp",tmp); replay_mkdir(folder);
    size_t n=strlen(folder); snprintf(folder+n,sizeof(folder)-n,"/logs");
    if(!open_directory(folder)) return false;
  }
  time_t now=time(NULL); struct tm *t=localtime(&now);
  if(!t || !strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",t)) strcpy(stamp,"session");
  snprintf(stem,sizeof(stem),"%s/mmx-replay-%s-%ld",folder,stamp,(long)replay_pid());
  atexit(MmxReplayClose);
  return true;
}
static bool start_segment(void) {
  MmxReplayClose();
  if(!init_stem()) { error("cannot create replay directory"); return false; }
  ++segment;
  snprintf(path,sizeof(path),"%s-%06u.replay",stem,segment);
  char sidecar[1400]; snprintf(sidecar,sizeof(sidecar),"%s.sav",path);
  states=fopen(sidecar,"wb");
  if(!states) { error("cannot open checkpoint file"); return false; }
  stream=fopen(path,"w"); if(!stream) { error("cannot open input log"); return false; }
  next_frame=(unsigned)snes_frame_counter; generation=RtlStateGeneration();
  fprintf(stream,"MMXREPLAY 1\nBUILD %s %s %s\nROM %08x\n",
      SNESRECOMP_BUILD_VERSION,MMX_REPLAY_REVISION,MMX_REPLAY_ENGINE,
      g_snes && g_snes->cart ? hash(g_snes->cart->rom,g_snes->cart->romSize):0);
  fprintf(stream,"MODS %u\n",mods());
  /* These copies document the exact selected mods/options. The replay helper
   * stages them in its own directory; it never overwrites personal settings. */
  snprintf(sidecar,sizeof(sidecar),"%s.config.ini",path); copy(config_path,sidecar);
  snprintf(sidecar,sizeof(sidecar),"%s.mods.toml",path); copy("mods/preloaded/state.toml",sidecar);
  /* Netplay temporarily adopts the host's options without rewriting the
   * offline TOML. Keep the active selection as well as local source paths. */
  char active[16384];int length=snes_mod_runtime_effective_set_c(active,sizeof(active));
  snprintf(sidecar,sizeof(sidecar),"%s.mods.txt",path);
  FILE *selection=fopen(sidecar,"w");
  if(length<0 || length>=(int)sizeof(active) || !selection) {
    if(selection) fclose(selection);
    error("cannot save the active mod selection");return false;
  }
  bool selected=fwrite(active,1,(size_t)length,selection)==(size_t)length;
  if(fclose(selection) || !selected) {error("cannot write active mod selection");return false;}
  fflush(stream);
  fprintf(stderr,"[mmx-replay] recording %s (attach this file and its sidecars)\n",path);
  if(segment>SEGMENTS) {
    static const char *suffix[]={"",".sav",".config.ini",".mods.toml",".mods.txt"};
    for(unsigned i=0;i<5;++i) {
      snprintf(sidecar,sizeof(sidecar),"%s-%06u.replay%s",stem,segment-SEGMENTS,suffix[i]); remove(sidecar);
    }
  }
  return true;
}
bool MmxReplayPlayback(const char *name) {
  MmxReplayClose(); playing=true; finished=failed=playback_loaded=false;
  playback_checkpointed=false;playback_rows=0;
  if(!name || !snesrecomp_abspath(name,path,sizeof(path))) { error("invalid replay path"); return false; }
  stream=fopen(path,"r"); if(!stream) { error("cannot open replay"); return false; }
  return true;
}
static bool load_playback(void) {
  char line[512],version[80],revision[80],engine[80],sidecar[1400]; unsigned format,rom,selection;
  if(!fgets(line,sizeof(line),stream) || sscanf(line,"MMXREPLAY %u",&format)!=1 || format!=1 ||
      !fgets(line,sizeof(line),stream) || sscanf(line,"BUILD %79s %79s %79s",version,revision,engine)!=3 ||
      !fgets(line,sizeof(line),stream) || sscanf(line,"ROM %x",&rom)!=1 ||
      !g_snes || !g_snes->cart || rom!=hash(g_snes->cart->rom,g_snes->cart->romSize) ||
      !fgets(line,sizeof(line),stream) || sscanf(line,"MODS %u",&selection)!=1) {
    error("invalid header or different ROM"); return false;
  }
  fprintf(stderr,"[mmx-replay] recorded build %s revision %s engine %s; replay build %s revision %s engine %s\n",
      version,revision,engine,SNESRECOMP_BUILD_VERSION,MMX_REPLAY_REVISION,MMX_REPLAY_ENGINE);
  if(selection!=mods()) { error("enable the recorded character, controls, weapons and co-op mods before replay"); return false; }
  if(snes_runahead_active()) { error("disable run-ahead before replay"); return false; }
  snprintf(sidecar,sizeof(sidecar),"%s.sav",path);
  states=fopen(sidecar,"rb");
  if(!states) { error("cannot open checkpoint file"); return false; }
  playback_loaded=true; return true;
}
static bool allocate_snapshot(size_t size) {
  if(!size || size>SNAPSHOT_LIMIT) return false;
  if(size>snapshot_cap) {
    void *bytes=realloc(snapshot,size); if(!bytes) return false;
    snapshot=bytes; snapshot_cap=size;
  }
  return true;
}
static bool write_snapshot(void) {
  size_t size=RtlSaveSnapshotToMemory(NULL,0);
  long offset=ftell(states);
  if(offset<0 || !allocate_snapshot(size) ||
      RtlSaveSnapshotToMemory(snapshot,size)!=size ||
      fwrite(snapshot,1,size,states)!=size || fflush(states) ||
      fprintf(stream,"S %ld %zu %08x\n",offset,size,hash(snapshot,size))<0) {
    error("cannot write machine checkpoint"); return false;
  }
  return true;
}
static uint32_t playback_begin(uint32_t inputs) {
  if(!playback_loaded && !load_playback()) return inputs;
  char line[256]; unsigned frame,word,flags; int width,peer;
  if(!fgets(line,sizeof(line),stream)) {
    if(ferror(stream)) error("input read failed");
    else if(!playback_rows) error("replay contains no completed frames");
    else { finished=true; MmxReplayClose(); fprintf(stderr,"[mmx-replay] all recorded checkpoints matched\n"); }
    return inputs;
  }
  if(line[0]=='S') {
    unsigned offset,size,digest;
    if(sscanf(line,"S %u %u %x",&offset,&size,&digest)!=3 || offset>STATE_LIMIT ||
        !allocate_snapshot(size) || fseek(states,(long)offset,SEEK_SET) ||
        fread(snapshot,1,size,states)!=size || hash(snapshot,size)!=digest ||
        !RtlLoadSnapshotFromMemory(snapshot,size) || !fgets(line,sizeof(line),stream)) {
      error("missing, damaged or incompatible machine checkpoint"); return inputs;
    }
    playback_checkpointed=true;
  }
  if(!playback_checkpointed || sscanf(line,"I %u %x %x %d %d",&frame,&word,&flags,&width,&peer)!=5 || flags>127 ||
      width<256 || width>MMX_RENDER_MAX_WIDTH || (width&1) || peer < -1 || peer>1) {
    error("invalid input record"); return inputs;
  }
  if(frame!=(unsigned)snes_frame_counter) {
    error("frame discontinuity has no machine checkpoint"); return inputs;
  }
  g_config.widescreen=(flags&1)!=0; g_mmx_custom_renderer=(flags&2)!=0;
  g_config.new_renderer=(flags&4)!=0; g_config.no_sprite_limits=(flags&8)!=0;
  MmxCoopViewsSetIndependent((flags&16)!=0); MmxCoopViewsSetOnline((flags&64)!=0);
  if(flags&32) RtlEnableExtendedFrameTiming();
  if(!!(flags&32)!=rtl_apu_extended_frame_timing()) { error("frame timing mode differs"); return inputs; }
  MmxRendererSetPeerView(peer); playback_width=width;
  MmxReplayViewport(&g_mmx_custom_view.width,&g_mmx_custom_view.extra);
  next_frame=frame+1; pending=true; return word;
}
static uint32_t record_begin(uint32_t inputs) {
  static int env_record=-1;
  /* Automatic capture must also work after an upgrade with saved Developer
   * options disabled. MMX_REPLAY_RECORD=0 opts out of this default; explicitly
   * enabled diagnostic mods can still request a capture. */
#if defined(__SWITCH__)
  /* Not on Switch: every frame would hash WRAM/VRAM and flush two writes to
   * the SD card, and every state load or rewind would write a snapshot. The
   * Developer diagnostics mods still turn capture on. */
  if(env_record<0) env_record=0;
#else
  if(env_record<0) { const char *e=getenv("MMX_REPLAY_RECORD"); env_record=!e || *e!='0'; }
#endif
  if(failed || finished) return inputs;
  if(!enabled && !env_record && !g_mmx_coop_trace) return inputs;
  if(snes_runahead_active() || RtlSpeculativeFrame()) {
    static bool warned;
    if(!warned) fprintf(stderr,"[mmx-replay] capture suspended: run-ahead/speculative rendering is not supported\n");
    warned=true; MmxReplayClose(); return inputs;
  }
  unsigned frame=(unsigned)snes_frame_counter;
  bool discontinuity=frame!=next_frame || generation!=RtlStateGeneration() || recorded_load_sequence!=load_sequence;
  if(!stream || rows>=SEGMENT_FRAMES || (discontinuity && ftell(states)>STATE_LIMIT-SNAPSHOT_LIMIT))
    if(!start_segment()) return inputs;
  if((!rows || discontinuity) && !write_snapshot()) return inputs;
  generation=RtlStateGeneration();
  recorded_load_sequence=load_sequence;
  if(fprintf(stream,"I %u %08x %02x %d %d\n",frame,inputs,policy(),
      g_mmx_custom_view.width,MmxRendererGetPeerView())<0 || fflush(stream)) {
    error("cannot write input record"); return inputs;
  }
  pending=true; next_frame=frame+1; return inputs;
}
bool MmxReplayBegin(uint32_t *inputs) {
  if(playing) {
    if(finished || failed) return false;
    *inputs=playback_begin(*inputs);
    return !finished && !failed;
  }
  record_begin(*inputs);
  return true; /* Optional recording failures never stop the player's game. */
}
void MmxReplayEnd(void) {
  if(!pending || failed || finished) return;
  pending=false; uint32_t w,c,v,p; hashes(&w,&c,&v,&p);
  if(playing) {
    /* Execute even the last flushed input of a hung/crashed session. Reading
     * its result afterwards lets that frame reproduce the original fault;
     * if it returns, absence of a checksum is still an incomplete replay. */
    char line[256];unsigned after;
    if(!fgets(line,sizeof(line),stream) ||
        sscanf(line,"H %u %x %x %x %x",&after,&expected_wram,&expected_coop,&expected_views,&expected_vram)!=5 ||
        after!=next_frame) {
      error("incomplete or invalid checkpoint after input (the recorded frame may have hung/crashed)");return;
    }
    if((unsigned)snes_frame_counter!=next_frame || w!=expected_wram || c!=expected_coop || v!=expected_views || p!=expected_vram) {
      fprintf(stderr,"[mmx-replay] FIRST DIVERGENCE frame=%u WRAM=%08x/%08x co-op=%08x/%08x views=%08x/%08x VRAM=%08x/%08x\n",
          next_frame-1,w,expected_wram,c,expected_coop,v,expected_views,p,expected_vram);
      if(RtlSaveSnapshot("replay-divergence.sav"))
        fprintf(stderr,"[mmx-replay] saved actual divergent machine to replay-divergence.sav\n");
      error("checkpoint mismatch");
    } else {
      ++playback_rows;
      int next=fgetc(stream);
      if(next==EOF) {
        if(ferror(stream)) error("input read failed");
        else { finished=true; MmxReplayClose(); fprintf(stderr,"[mmx-replay] all recorded checkpoints matched\n"); }
      } else ungetc(next,stream);
    }
  } else if(stream) {
    if(fprintf(stream,"H %u %08x %08x %08x %08x\n",(unsigned)snes_frame_counter,w,c,v,p)<0 || fflush(stream)) error("cannot write checkpoint");
    ++rows;
  }
}
