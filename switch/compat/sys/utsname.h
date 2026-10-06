/* Minimal <sys/utsname.h> for libnx/newlib, which has none. Only the crash
 * report reads it, to label the platform. */
#ifndef MMX_SWITCH_COMPAT_SYS_UTSNAME_H
#define MMX_SWITCH_COMPAT_SYS_UTSNAME_H
#include <string.h>

struct utsname {
  char sysname[32];
  char nodename[32];
  char release[32];
  char version[32];
  char machine[32];
};

static inline int uname(struct utsname *u) {
  memset(u, 0, sizeof(*u));
  strcpy(u->sysname, "horizon");
  strcpy(u->nodename, "switch");
  strcpy(u->release, "libnx");
  strcpy(u->machine, "aarch64");
  return 0;
}
#endif
