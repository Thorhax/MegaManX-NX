/* Nintendo Switch entry point.
 *
 * libnx calls main() after the homebrew loader has set up the environment.
 * This shim brings up the console-side services the desktop host assumes are
 * simply there (sockets for nxlink logging, a writable working directory next
 * to the NRO, a ROM path in place of a file picker) and then runs the shared
 * desktop host unchanged. */
#include <switch.h>

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

int mmx_desktop_main(int argc, char **argv);
int snesrecomp_rom_cache_read(char *path_out, size_t max_len);
int snesrecomp_rom_cache_write(const char *rom_path);

/* No "sdmc:" device prefix: libnx resolves "/..." against the SD card, and the
 * shared host treats a leading '/' as the only absolute-path spelling. */
#define MMX_SWITCH_DEFAULT_DIR "/switch/MegaManX/"
#define MMX_SWITCH_DEFAULT_NRO MMX_SWITCH_DEFAULT_DIR "MegaManX.nro"
#define MMX_SWITCH_ROM_NAME "mmx.sfc"
#ifndef MMX_SWITCH_LAUNCHER
#define MMX_SWITCH_LAUNCHER 0
#endif

static char s_exe_path[1024] = MMX_SWITCH_DEFAULT_NRO;
static char s_exe_dir[1024] = MMX_SWITCH_DEFAULT_DIR;
static char s_rom_path[1024];
static int s_nxlink_fd = -1;

/* host_paths.c asks this for "the executable", which on Switch is the NRO the
 * homebrew loader started. Everything the host keeps beside the executable
 * (config.ini, rom.cfg, saves/, mods/) therefore lives in its folder. */
const char *snesrecomp_switch_exe_path(void) { return s_exe_path; }

static void ResolveExePath(int argc, char **argv) {
  /* hbloader passes the NRO's full path as argv[0]; netloaded builds may not,
   * and then keep the default install location. */
  if (argc > 0 && argv[0] && strchr(argv[0], '/') &&
      strlen(argv[0]) < sizeof(s_exe_path)) {
    const char *nro = argv[0];
    if (!strncmp(nro, "sdmc:", 5)) nro += 5;
    snprintf(s_exe_path, sizeof(s_exe_path), "%s", nro);
  }
  snprintf(s_exe_dir, sizeof(s_exe_dir), "%s", s_exe_path);
  char *slash = strrchr(s_exe_dir, '/');
  if (slash) slash[1] = '\0';
  mkdir(s_exe_dir, 0777);
  chdir(s_exe_dir);
}

static int HasRomExtension(const char *name) {
  size_t n = strlen(name);
  return n > 4 && (!strcasecmp(name + n - 4, ".sfc") ||
                   !strcasecmp(name + n - 4, ".smc"));
}

/* mmx.sfc beside the NRO wins; otherwise the first SNES dump of Mega Man X's
 * size (1.5 MiB, optionally with a 512-byte copier header). The host checks
 * the SHA-256 either way, so a wrong pick is refused, never run. */
static int FindRom(void) {
  struct stat st;
  snprintf(s_rom_path, sizeof(s_rom_path), "%s%s", s_exe_dir, MMX_SWITCH_ROM_NAME);
  if (stat(s_rom_path, &st) == 0) return 1;
  DIR *dir = opendir(s_exe_dir);
  if (!dir) return 0;
  struct dirent *e;
  int found = 0;
  while (!found && (e = readdir(dir)) != NULL) {
    if (!HasRomExtension(e->d_name)) continue;
    snprintf(s_rom_path, sizeof(s_rom_path), "%s%s", s_exe_dir, e->d_name);
    if (stat(s_rom_path, &st) == 0 &&
        (st.st_size == 0x180000 || st.st_size == 0x180000 + 512))
      found = 1;
  }
  closedir(dir);
  if (!found) s_rom_path[0] = '\0';
  return found;
}

static void ShowFatal(const char *title, const char *body) {
  consoleInit(NULL);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);
  printf("\x1b[2J\x1b[1;1H");
  printf("Mega Man X Recompiled (Switch)\n\n");
  printf("\x1b[31m%s\x1b[0m\n\n%s\n\nPress + to exit.\n", title, body);
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    consoleUpdate(NULL);
  }
  consoleExit(NULL);
}

#define MMX_SWITCH_ERR_LOG "logs/mmx-switch-err.log"

static void OpenLog(void) {
  if (s_nxlink_fd >= 0) return;   /* nxlink owns stdout/stderr */
  mkdir("logs", 0777);
  /* Keep the previous run: a failed start is usually followed by a working
   * one, which would otherwise overwrite the only record of the failure. */
  remove("logs/mmx-switch.prev.log");
  remove("logs/mmx-switch-err.prev.log");
  rename("logs/mmx-switch.log", "logs/mmx-switch.prev.log");
  rename(MMX_SWITCH_ERR_LOG, "logs/mmx-switch-err.prev.log");
  freopen("logs/mmx-switch.log", "w", stdout);
  freopen(MMX_SWITCH_ERR_LOG, "w", stderr);
}

/* The last lines of this run's error log, for the failure screen. */
static void LogTail(char *out, size_t cap, int lines) {
  out[0] = '\0';
  if (s_nxlink_fd >= 0) return;
  FILE *f = fopen(MMX_SWITCH_ERR_LOG, "rb");
  if (!f) return;
  static char buf[16384];
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  long start = size > (long)sizeof(buf) - 1 ? size - (long)sizeof(buf) + 1 : 0;
  fseek(f, start, SEEK_SET);
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';
  char *p = buf + n;
  int seen = 0;
  while (p > buf && seen <= lines) if (*--p == '\n') ++seen;
  if (*p == '\n') ++p;
  /* The console is 80 columns: clip long lines rather than wrap them. */
  size_t o = 0;
  int col = 0;
  for (; *p && o + 2 < cap; ++p) {
    if (*p == '\n') { out[o++] = '\n'; col = 0; continue; }
    if (col++ < 78) out[o++] = *p;
  }
  out[o] = '\0';
}

int main(int argc, char **argv) {
  /* Keep the main thread (guest CPU, PPU, presentation) on core 0. The
   * custom renderer's row workers take cores 1 and 2 (mmx_renderer.c), so
   * the scheduler no longer moves the main thread between busy cores. */
  svcSetThreadCoreMask(CUR_THREAD_HANDLE, 0, 1u << 0);
  if (R_SUCCEEDED(socketInitializeDefault())) {
    s_nxlink_fd = nxlinkStdio();
    if (s_nxlink_fd < 0) socketExit();
  }
  ResolveExePath(argc, argv);
  OpenLog();
  fprintf(stderr, "[switch] exe=%s dir=%s\n", s_exe_path, s_exe_dir);

  /* A chain-loaded restart (the in-game launcher, see host_relaunch.c)
   * carries the host's own flags; pass them through untouched. */
  int relaunched = argc > 1;
  int have_rom = FindRom();
  if (have_rom) fprintf(stderr, "[switch] rom=%s\n", s_rom_path);

#if MMX_SWITCH_LAUNCHER
  /* The launcher opens on the cached ROM (rom.cfg), and lets the player
   * browse the SD card for one when there is none. */
  if (have_rom && !relaunched) {
    char cached[1024];
    struct stat st;
    if (!snesrecomp_rom_cache_read(cached, sizeof(cached)) || stat(cached, &st) != 0)
      snesrecomp_rom_cache_write(s_rom_path);
  }
#else
  if (!have_rom) {
    char body[1600];
    snprintf(body, sizeof(body),
             "No Mega Man X (USA) (Rev 1) ROM was found.\n\n"
             "Copy your own legally dumped ROM to:\n  %s%s\n",
             s_exe_dir, MMX_SWITCH_ROM_NAME);
    ShowFatal("ROM missing", body);
    if (s_nxlink_fd >= 0) socketExit();
    return 1;
  }
#endif

  /* argv[0] is the executable. Without the launcher the positional ROM boots
   * straight in; the shared host verifies it against the generated code's
   * SHA-256 either way. */
  char *host_argv[16] = { s_exe_path, NULL };
  int host_argc = 1;
  if (relaunched) {
    for (int i = 1; i < argc && host_argc < 15; ++i) host_argv[host_argc++] = argv[i];
  } else if (!MMX_SWITCH_LAUNCHER) {
    host_argv[host_argc++] = s_rom_path;
  }
  host_argv[host_argc] = NULL;
  int rc = mmx_desktop_main(host_argc, host_argv);

  fflush(stdout);
  fflush(stderr);
  if (rc != 0) {
    char tail[2400], body[3200];
    LogTail(tail, sizeof(tail), 14);
    snprintf(body, sizeof(body),
             "The game exited with code %d. ROM: %s\n"
             "Logs: %slogs/ (send mmx-switch-err.log)\n\n"
             "Last log lines:\n%s", rc, s_rom_path, s_exe_dir, tail);
    ShowFatal("Startup failed", body);
  }
  if (s_nxlink_fd >= 0) {
    close(s_nxlink_fd);
    socketExit();
  }
  return rc;
}
