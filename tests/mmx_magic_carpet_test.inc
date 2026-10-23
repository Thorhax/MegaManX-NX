/* Original 2015 controller demonstration, replayed against the real ROM.
 * Only the stage and acquired equipment are arranged; pickup, allocation,
 * Boomerang movement and platform contact all execute the guest code. */
static unsigned carpet_word(unsigned a) { return g_ram[a] | g_ram[a+1] << 8; }
static void carpet_frames(unsigned n,unsigned pad) { while(n--) frame(pad); }
static void magic_carpet_checks(const char *path,uint8 *start,uint8 *expected,uint8 *actual,size_t cap) {
  const char *aspect=getenv("MMX_MAGIC_CARPET_WIDE");bool wide=aspect!=NULL;
  bool no_shot=getenv("MMX_MAGIC_CARPET_NO_SHOT")!=NULL;
  g_config.widescreen=wide;
  g_mmx_custom_aspect=aspect && !strcmp(aspect,"32:9")?MMX_ASPECT_32_9:MMX_ASPECT_16_9;
  int width,height;MmxPrepareFrame(1280,720,&width,&height);
  for(unsigned i=0;i<20;++i) {
    frame(SNES_PAD_START);carpet_frames(119,0);
    if(g_ram[0xd1]==2 && g_ram[0xd2]==4) break;
  }
  check(g_ram[0xd1]==2 && g_ram[0xd2]==4,"carpet boots through title");
  g_ram[0xd1]=2;g_ram[0xd2]=2;g_ram[0xd3]=g_ram[0xd4]=0;
  g_ram[0x1f7a]=5;g_ram[0x1f81]=g_ram[0x1f82]=0;
  /* $81:9D88..9DC7 creates armor actors at stage entry. Grant the boots
   * now; changing ownership afterwards leaves their dash body absent. */
  g_ram[0x1f99]=8;g_ram[0x1f94]=0xc0;
  for(unsigned i=0;i<30;++i) {
    carpet_frames(60,0);
    if(g_ram[0xd3]==4 && g_ram[0xba9]==2) break;
  }
  check(g_ram[0x1f7a]==5 && g_ram[0xd3]==4 && g_ram[0xba9]==2,"carpet enters Storm Eagle");
  carpet_frames(60,0);
  static const uint8 loadout[]={0,0,0xdc,0,0,0,0,0,0,0,0,0,0xdc,0,0xdc,0,0xdc,8,16,4,0,0,0,0};
  memcpy(g_ram+0x1f88,loadout,sizeof(loadout));
  check(g_ram[0xc78] && g_ram[0xc8e]==0x5f,"stage entry creates the acquired dash boots actor");
  printf("CARPET start wide=%u xy=%u,%u mode=%02x%02x%02x%02x\n",wide,carpet_word(0xbad),carpet_word(0xbb0),g_ram[0xd1],g_ram[0xd2],g_ram[0xd3],g_ram[0xd4]);
  FILE *inputs=fopen(path,"r");check(inputs!=NULL,"original carpet inputs open");
  FILE *trace=fopen("magic-carpet.csv","w");check(trace!=NULL,"carpet trace opens");
  fputs("frame,pad,x,y,camera_x,camera_y,shot,item,class,intro_clear\n",trace);
  char line[256];unsigned tick=0,heart_frames=0,carpet_ticks=0,first_carpet=0,first_y=0,min_y=65535;
  unsigned heart_shot=0,heart_slot=0;size_t active_size=0;bool same_reference=true;
  while(fgets(line,sizeof(line),inputs)) {
    if(line[0]=='#') continue;
    unsigned count,word;check(sscanf(line,"%u,%x",&count,&word)==2 && count && count<=1800 && word<4096,"carpet input range valid");
    unsigned pad=0;for(unsigned j=0;j<12;++j) if(word&(1u<<(11-j))) pad|=1u<<j;
    if(no_shot) pad&=~SNES_PAD_Y;
    while(count--) {
      frame(pad);unsigned shot=0,item=0,kind=0;
      if(wide && tick>=724 && tick<=739) {
        /* Left margin exposes the heart before the native pass allocates it
         * at sample 728. Keep the art through initialization and submission. */
        static uint32_t preview[MMX_RENDER_MAX_WIDTH*224];
        check(MmxRendererDraw(preview,g_mmx_custom_view,false),"Storm margin preview renders");
        check(MmxRendererGetStats().margin_sprite_pixels>0,"Storm heart stays visible across the native spawn handoff");
        if(tick==724) {
          bool heart=false;
          for(unsigned d=0x1628;d<0x1928;d+=48) heart|=g_ram[d] && g_ram[d+10]==11;
          check(!heart,"visible Storm preview does not allocate the heart early");
        }
      }
      if(tick==60) {
        check(g_ram[0xbaa]==0x14 && g_ram[0xc78] && g_ram[0xc8f]==0x38,"dash fixture submits the real boots pose");
        unsigned x=carpet_word(0xbad)-carpet_word(0x1e4d)-6;
        unsigned y=carpet_word(0xbb0)-carpet_word(0x1e50)+8;
        /* A lower-leg highlight, where the broken fixture showed green
         * runway. Check the rendered field as well as actor ownership. */
        const uint32_t *native=(const uint32_t *)g_my_pixels;
        check(x<256 && y<224 && (native[y*256+x]&0xffffff)==0x9c9c9c,"native dash shows X's lower leg");
        if(wide) {
          static uint32_t pixels[MMX_RENDER_MAX_WIDTH*224];
          check(MmxRendererDraw(pixels,g_mmx_custom_view,false),"dash compositor renders");
          check((pixels[y*g_mmx_custom_view.width+g_mmx_custom_view.extra+x]&0xffffff)==0x9c9c9c,"widescreen dash preserves X's lower leg");
        }
      }
      for(unsigned d=0x1228;d<0x1428;d+=64) {
        if(!g_ram[d] || g_ram[d+10]!=13) continue;
        unsigned held=carpet_word(d+12);
        if(held<0x1628 || held>=0x1928 || (held-0x1628)%48 || !g_ram[held]) continue;
        shot=d;item=held;kind=g_ram[held+10];
        if(kind==11) {++heart_frames;heart_shot=d;heart_slot=held;}
        if(kind==14) {
          same_reference&=d==heart_shot && held==heart_slot;
          if(!carpet_ticks) {first_carpet=tick;first_y=carpet_word(0xbb0);}
          ++carpet_ticks;if(carpet_word(0xbb0)<min_y)min_y=carpet_word(0xbb0);
        }
      }
      fprintf(trace,"%u,%03x,%u,%u,%u,%u,%04x,%04x,%u,%02x\n",tick,pad,carpet_word(0xbad),carpet_word(0xbb0),carpet_word(0x1e4d),carpet_word(0x1e50),shot,item,kind,g_ram[0x1f9b]);
      if(tick%120==0) printf("CARPET %u xy=%u,%u shot=%04x item=%04x class=%u hearts=%02x\n",tick,carpet_word(0xbad),carpet_word(0xbb0),shot,item,kind,g_ram[0x1f9b]);
      if(!no_shot && wide && (tick==60 || tick==900 || tick==1500)) {
        char suffix[64];snprintf(suffix,sizeof(suffix),".carpet-%u.cap",tick);
        zero_capture(getenv("MMX_ZERO_TEST_CAPTURE"),suffix);
      }
      if(tick==760 || tick==1500) {
        const char *name=tick==760?"magic-carpet-setup.sav":"magic-carpet-active.sav";
        check(RtlSaveSnapshot(name),"private carpet playtest snapshot saves");
      }
      if(!no_shot && tick==1500) {
        check(kind==14,"playtest snapshot contains the active carpet");
        check(g_mmx_custom_renderer && g_snes_width==g_mmx_custom_view.width && (wide || g_snes_width==256),"upper sky repair activates at the selected aspect");
        active_size=RtlSaveSnapshotToMemory(start,cap);
      }
      ++tick;
    }
  }
  fclose(inputs);check(fclose(trace)==0,"carpet trace writes");
  printf("CARPET result input=%u heart=%u carpet=%u first=%u y=%u minimum=%u\n",tick,heart_frames,carpet_ticks,first_carpet,first_y,min_y);
  check(tick==1800,"complete original carpet input sequence");
  if(no_shot) {
    check(!heart_frames && !carpet_ticks,"with shooting removed there is no phantom grab or carpet");
    puts("MMX MAGIC CARPET NEGATIVE CHECKS PASSED");return;
  }
  check(heart_frames>0,"Boomerang carries the actual heart");
  check(same_reference,"original heart reference is reused for platform");
  check(g_ram[0x1f9a]==18,"original heart collection increases maximum health");
  check(carpet_ticks>=120,"same Boomerang carries a native moving platform");
  check(first_y>min_y+200,"platform ride gains at least 200 pixels of altitude");
  check(active_size>0,"active carpet snapshot saves");
  check(RtlLoadSnapshotFromMemory(start,active_size),"active carpet snapshot restores");
  carpet_frames(30,SNES_PAD_RIGHT);size_t en=RtlSaveSnapshotToMemory(expected,cap);
  check(RtlLoadSnapshotFromMemory(start,active_size),"active carpet snapshot restores again");
  carpet_frames(30,SNES_PAD_RIGHT);size_t an=RtlSaveSnapshotToMemory(actual,cap);
  same(expected,en,actual,an,"active carpet replays deterministically");
  if(getenv("MMX_RASTER_INVARIANTS")) {
    check(RtlRollbackSnapshotBound()<=cap,"rollback invariant buffers fit");
    en=RtlRollbackSaveToMemory(expected,cap);
    uint64_t clocks=g_cpu.master_cycles;
    static uint32_t pixels[MMX_RENDER_MAX_WIDTH*224];
    const MmxRenderView views[]={{256,0,4.0/3.0},{682,213,32.0/9.0},{342,43,16.0/9.0}};
    for(unsigned repeat=0;repeat<2;++repeat) for(unsigned v=0;v<3;++v)
      check(MmxRendererDraw(pixels,views[(v+repeat)%3],repeat!=0),"repeated render at another width succeeds");
    an=RtlRollbackSaveToMemory(actual,cap);
    same(expected,en,actual,an,"render count and view width leave complete rollback state unchanged");
    check(g_cpu.master_cycles==clocks,"rendering consumes no guest clocks");
  }
  MmxRasterStats raster=MmxRendererGetRasterStats();
  printf("MMX_RASTER_STATS {\"images\":%u,\"copied_bytes\":%llu,\"allocated_bytes\":%zu,\"metadata_bytes\":%zu,\"compared_bytes\":%llu}\n",
      raster.vram_images,(unsigned long long)raster.vram_copied_bytes,
      raster.vram_allocated_bytes,raster.frame_metadata_bytes,
      (unsigned long long)raster.vram_compared_bytes);
  puts("MMX MAGIC CARPET CHECKS PASSED");
}
