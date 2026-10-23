static void coop_special_graphics(void) {
  /* Wrong shared CHR/CGRAM must never replace either seat's special.
   * Check both projection orders, including optional asset repairs off. */
  memset(rom_bytes,0,sizeof(rom_bytes));
  for(unsigned w=1;w<=8;++w) {
    unsigned list=0x31000+w*16,palette=0x31300+w*8,source=0x58000+w*32;
    rom_word(0x318c5 + 0x3e + w*2,list&0xffff);
    rom_word(list,32);rom_word(list+2,0x6200);rom_long(list+4,0x8b8000+w*32);
    rom_word(list+7,1);
    for(unsigned y=0;y<8;++y)rom_bytes[source+y*2]=255;
    rom_word(0x30133+0x40+w*2,palette&0xffff);
    rom_word(0x30133+0x100+w*2,palette&0xffff);
    rom_bytes[palette]=16;rom_word(palette+1,0x9000+w*32);rom_bytes[palette+3]=128;
    rom_word(0x29000+w*32+2,w&1?31:31<<5);
    unsigned group=0x40+w,arrangement=0x6a000+w*8;
    rom_long(0x68000+group*3,0x8d9000+w*4);
    rom_long(0x69000+w*4,0x8da000+w*8);
    rom_bytes[arrangement]=1;rom_bytes[arrangement+3]=0x1d;
  }
  MmxZeroDisable();MmxBossRushReset();MmxCoopViewsSetOnline(false);
  MmxRendererSetRom(NULL,0);MmxRendererSetRom(rom_bytes,sizeof(rom_bytes));
  g_mmx_custom_renderer=true;
  for(unsigned weapon=1;weapon<=8;++weapon)for(unsigned charged=0;charged<2;++charged)
  for(unsigned anchor=0;anchor<2;++anchor)for(unsigned repairs=0;repairs<2;++repairs) {
    memset(&ppu,0,sizeof(ppu));memset(ram,0,sizeof(ram));memset(stock,0,sizeof(stock));
    MmxRendererReset();g_mmx_render_asset_repairs=repairs!=0;
    ram[0xd1]=2;ram[0xd2]=ram[0xd3]=4;
    ppu.inidisp=15;ppu.bgmode=1;ppu.screenEnabled[0]=16;ppu.obsel=3;
    for(unsigned i=0;i<128;++i)ppu.oam[i*2]=0xe000;
    for(unsigned i=0;i<256;++i)ppu.cgram[i]=31<<10;
    memset(ppu.vram,255,sizeof(ppu.vram));
    MmxCoopState c={0};c.initialized=1;c.current=c.anchor=(uint8_t)anchor;
    for(unsigned seat=0;seat<2;++seat) {
      unsigned w=seat?weapon%8+1:weapon;
      MmxCoopPlayer *p=&c.players[seat];p->status=MMX_COOP_ALIVE;p->body[0x27]=32;
      uint8_t *b=p->shots;b[0]=1;b[1]=2;b[10]=(uint8_t)(w+(charged?15:6));
      b[14]=128;b[17]=0x26;b[22]=(uint8_t)(0x40+w);b[24]=3;
      b[5]=(uint8_t)(60+seat*100);b[8]=120;
    }
    memcpy(ram+0x1228,c.players[anchor].shots,0x200);
    unsigned x=ram[0x122d],w=anchor?weapon%8+1:weapon;
    put_word(0,x);put_word(2,120);ram[0xf]=0x26;ram[0x10]=3;
    put_word(0x18,0xa000+w*8);ram[0x1a]=0x8d;
    MmxRendererObserveObject(ram,0x1228);MmxRendererRecordPiece(ram,0);MmxRendererLatchSprites();
    ppu.oam[32]=(uint16_t)(120<<8|x);ppu.oam[33]=0x2620;
    capture();MmxRendererCoopFrame(&c);
    assert(MmxRendererDraw(output,(MmxRenderView){256,0,4.0/3.0},false));
    for(unsigned seat=0;seat<2;++seat) {
      unsigned own=seat?weapon%8+1:weapon;
      if(output[120*256+60+seat*100]!=(own&1?0xff0000u:0x00ff00u))
        fprintf(stderr,"special graphics weapon=%u charged=%u anchor=%u repairs=%u seat=%u pixel=%06x\n",
            weapon,charged,anchor,repairs,seat,output[120*256+60+seat*100]);
      assert(output[120*256+60+seat*100]==(own&1?0xff0000u:0x00ff00u));
    }
  }
  g_mmx_render_asset_repairs=true;g_mmx_custom_renderer=false;MmxRendererSetRom(NULL,0);
}
