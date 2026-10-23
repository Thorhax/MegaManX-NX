/* X3 $81:A3xx/$B915 and $82:DB3F..DDD2. Utility response codes are
 * deliberately not damage. See docs/weapons/gravity-well.md.
 * Shot variants 0 well/orb, 1 body; effect variants 2 enemy, 3 particles.
 * Victim origin_x/y retain native slot/type, vx retains anchored X. */
static bool gravity_rush_boss(unsigned object) {
  int seat=MmxBossRushOwner(object);
  return seat>=0 && MmxBossRushGetState().bosses[seat].object==object;
}
const MmxWeaponShot *MmxWeaponsMovedEnemy(const MmxWeaponCombatState *c,unsigned enemy) {
  for(unsigned i=0;i<8;++i) {
    const MmxWeaponShot *s=c->shots+i;
    if(s->active && s->page==2 && s->weapon==2 && s->variant==1 && s->origin_x==(int)enemy) return s;
  }
  for(unsigned i=0;i<16;++i) {
    const MmxWeaponShot *s=c->effects+i;
    if(s->active && s->page==2 && s->weapon==6 && s->variant==2 &&
        s->origin_x==(int)enemy && !gravity_rush_boss(enemy)) return s;
  }
  return NULL;
}
static MmxWeaponShot *gravity_actor(unsigned variant) {
  for(unsigned i=0;i<8;++i) {
    MmxWeaponShot *s=combat.shots+i;
    if(s->active && s->page==2 && s->weapon==6 && s->variant==variant) return s;
  }
  return NULL;
}
static void gravity_player(uint8_t *r) {
  r[0xc0f]=1;
  MmxWeaponShot *s=gravity_actor(1);if(!s) return;
  stop_charge(r);r[0xbde]=r[0xbe2]=r[0xbdf]=r[0xbe3]=0;r[0xbf8]=0;
  putword(r+0xbc2,0);putword(r+0xbc4,0);
  if(r[0xbaa]!=14 && r[0xbaa]!=12) {r[0xbaa]=0;r[0xbab]=2;}
}
static unsigned gravity_angle(int dx,int dy) {
  /* Original 32-way $86:E18E directions; choose by cross-product against
   * the requested vector, using integer arithmetic in saved simulation. */
  int best=0;int64_t score=-1;
  for(unsigned a=0;a<32;++a) {
    int vx=ray_vector(a),vy=-ray_vector(a+8);
    int64_t dot=(int64_t)dx*vx+(int64_t)dy*vy;
    if(dot<=0) continue;
    int64_t candidate=dot*dot/(vx*vx+vy*vy);
    if(candidate>score) {score=candidate;best=(int)a;}
  }
  return (unsigned)best;
}
static int gravity_slow_vector(unsigned angle) {
  static const int16_t q[9]={0,93,171,230,271,307,343,372,384};
  unsigned a=angle&31,index=(a&8)?8-(a&7):(a&7);
  return (a&16)?-q[index]:q[index];
}
static bool gravity_host(const uint8_t *r,const MmxWeaponShot *s) {
  unsigned d=(unsigned)s->origin_x;
  return d>=0xe68 && d<0x1228 && (d&63)==0x28 && r[d] && r[d+1]==2 &&
      r[d+10]==s->origin_y && (r[d+0x27]&127) && !gravity_rush_boss(d);
}
static void gravity_destroy(uint8_t *r,MmxWeaponShot *s) {
  unsigned d=(unsigned)s->origin_x;
  /* X1's ordinary-enemy death state owns its explosion/drop/retirement.
   * Gravity is a scripted dissolve/lift kill, not a numeric weapon hit. */
  if(gravity_host(r,s)) {r[d+1]=4;r[d+2]=r[d+3]=r[d+0x27]=r[d+0x26]=0;}
  memset(s,0,sizeof(*s));
}
static void gravity_effect_tick(uint8_t *r,MmxWeaponShot *s) {
  if(s->variant==3) {
    s->y+=s->vy;++s->age;animation_step(s);
    if(!fang_visible(r,s)) memset(s,0,sizeof(*s));
    return;
  }
  if(!gravity_host(r,s)) {memset(s,0,sizeof(*s));return;}
  MmxWeaponShot *well=gravity_actor(0);
  if(!well || well->muzzle_pose!=2) {
    if(s->muzzle_pose==3) putword(r+s->origin_x+5,(unsigned)s->vx);
    memset(s,0,sizeof(*s));return;
  }
  ++s->age;
  if(s->muzzle_pose==3) { /* Large anchored enemies shake without damage. */
    static const int8_t shake[4]={0,1,0,-1};s->x=(s->vx+shake[s->age&3])*256;
  } else if(s->charged) {
    s->vy-=192;if(s->vy<-4096) s->vy=-4096;s->y+=s->vy;
    if((s->y>>8)<(int)camera_y(r)-32) {gravity_destroy(r,s);return;}
  } else {
    unsigned a=gravity_angle((well->x-s->x)>>8,(well->y-s->y)>>8);
    int vx,vy;
    if(s->age<=30) {vx=gravity_slow_vector(a)>>2;vy=-(gravity_slow_vector(a+8)>>2);}
    else if(s->age<=38) {vx=gravity_slow_vector(a);vy=-gravity_slow_vector(a+8);}
    else {int scale=s->age<=46?1:2;vx=ray_vector(a)/8*scale;vy=-ray_vector(a+8)/8*scale;}
    s->x+=vx;s->y+=vy;
    if(s->age>=60) {gravity_destroy(r,s);return;}
  }
  putword(r+s->origin_x+5,s->x>>8);putword(r+s->origin_x+8,s->y>>8);
}
static void gravity_capture(uint8_t *r,MmxWeaponShot *well) {
  for(unsigned d=0xe68;d<0x1228;d+=64) {
    /* Boss entrances temporarily use state 2 and an ordinary damage class.
     * Rush ownership is established at allocation, before those transient
     * bytes can make a boss look like a utility victim. Ordinary minions
     * remain eligible even when they share the boss's ownership tag. */
    if(gravity_rush_boss(d)) continue;
    if(!r[d] || r[d+1]!=2 || !(r[d+0x27]&127) || r[d+0x28]>=6 ||
        !word(r+d+0x20) || !MmxWeaponsEnemyActive(r,d,1)) continue;
    int x=(int)word(r+d+5)-(int)camera_x(r),y=(int)word(r+d+8)-(int)camera_y(r);
    if(x<0 || x>=256 || y<0 || y>=224) continue;
    unsigned box=word(r+d+0x20);
    size_t off=0x30000+(box&0x7fff);
    if(!collision_rom || off+4>collision_rom_size) continue;
    unsigned rx=collision_rom[off+2],ry=collision_rom[off+3];
    if(!rx || !ry || rx>32 || ry>32) continue;
    for(unsigned i=0;i<16;++i) if(!combat.effects[i].active) {
      MmxWeaponShot *s=combat.effects+i;memset(s,0,sizeof(*s));
      s->active=1;s->age=1;s->page=2;s->weapon=6;s->group=15;s->variant=2;
      s->charged=well->charged;s->origin_x=(int16_t)d;s->origin_y=r[d+10];
      s->x=(int)word(r+d+5)*256;s->y=(int)word(r+d+8)*256;s->vx=(int16_t)word(r+d+5);
      s->muzzle_pose=(rx>16 || ry>24)?3:0;break;
    }
  }
}
static void gravity_particle(uint8_t *r,unsigned age) {
  for(unsigned i=0;i<16;++i) if(!combat.effects[i].active) {
    MmxWeaponShot *s=combat.effects+i;memset(s,0,sizeof(*s));
    s->active=1;s->age=1;s->page=2;s->weapon=6;s->group=15;s->variant=3;
    s->x=((int)camera_x(r)+(int)(bubble_random(i)&127)+(age&4?128:0))*256;
    s->y=((int)camera_y(r)+224)*256;s->vy=-4096;
    animation_start(s,5+(bubble_random(i+1)&3));return;
  }
}
static void gravity_tick(uint8_t *r,unsigned d,MmxWeaponShot *s) {
  if(!s->age) {
    for(unsigned i=0;i<8;++i) {
      MmxWeaponShot *p=combat.shots+i;
      if(p!=s && p->active && p->page==2 && p->weapon==6) {retire(r,d);return;}
    }
    if(!MmxWeaponsSpend(2,6,256)) {retire(r,d);return;}
    s->age=1;s->born=combat.tick;muzzle_origin(r,d,s);
    if(s->charged) {
      stop_charge(r);animation_start(s,4);
      MmxWeaponShot *body=spawn_child(r,s);
      if(!body) {retire(r,d);return;}
      body->variant=1;body->group=(r[0xbd3]&4)?51:52;animation_start(body,body->group==51?35:36);
      native_object(r,0x1228+(unsigned)(body-combat.shots)*64,body);
      putword(r+0x1228+(unsigned)(body-combat.shots)*64+0x20,0);
    } else {s->vx=(int16_t)(s->facing?1024:-1024);animation_start(s,0);}
  } else if(s->born!=combat.tick) {
    ++s->age;
    if(s->charged && s->variant==1) {
      if(r[0xbaa]==14 || r[0xbaa]==12 || r[0x1f0c]) {retire(r,d);return;}
      MmxWeaponShot *well=gravity_actor(0);
      if(s->flags&64) {if(well && !well->muzzle_pose) {well->muzzle_pose=1;well->x=(int)word(r+0xbad)*256;well->y=((int)word(r+0xbb0)-27)*256;}}
      if(!(s->flags&32) || !well) {
        if((s->flags&128) && s->timer==1) {retire(r,d);return;}
        animation_step(s);
      }
    } else if(s->charged) {
      if(!gravity_actor(1)) {retire(r,d);return;}
      if(s->muzzle_pose==1) {
        s->vy-=192;s->y+=s->vy;animation_step(s);
        if((s->y>>8)<(int)camera_y(r)-16) {s->muzzle_pose=2;s->origin_x=300;}
      } else if(s->muzzle_pose==2) {
        if(!--s->origin_x) {retire(r,d);return;}
        gravity_capture(r,s);if(!(s->age&3)) gravity_particle(r,s->age);
      }
    } else if(!s->muzzle_pose) {
      s->vx+=(s->facing?-32:32);
      if(s->facing && s->vx<256) s->vx=256;
      if(!s->facing && s->vx>-256) s->vx=-256;
      s->x+=s->vx;animation_step(s);
      int travelled=(s->x>>8)-s->origin_x;if(travelled<0) travelled=-travelled;
      if(travelled>=64) {s->muzzle_pose=1;animation_start(s,1);}
    } else if(s->muzzle_pose==1 || s->muzzle_pose==3) {
      if((s->flags&128) && s->timer==1) {
        ++s->muzzle_pose;animation_start(s,s->muzzle_pose==2?2:0);s->radius=180;
      } else animation_step(s);
    } else if(s->muzzle_pose==2) {
      gravity_capture(r,s);animation_step(s);
      if(!--s->radius || !fang_visible(r,s)) {s->muzzle_pose=3;animation_start(s,3);}
    } else {
      int dx=(int)word(r+0xbad)-(s->x>>8),dy=(int)word(r+0xbb0)-(s->y>>8);
      if(dx>-12 && dx<12 && dy>-18 && dy<18) {retire(r,d);return;}
      unsigned a=gravity_angle(dx,dy);s->x+=ray_vector(a)/4;s->y-=ray_vector(a+8)/4;animation_step(s);
    }
  }
  if(s->age>700 || (!s->charged && s->muzzle_pose!=2 && !fang_visible(r,s))) {retire(r,d);return;}
  native_object(r,d,s);putword(r+d+0x20,0);
}
