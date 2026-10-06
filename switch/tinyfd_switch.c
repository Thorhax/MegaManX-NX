/* tinyfiledialogs needs a terminal and a shell (termios, popen), neither of
 * which Horizon has. On Switch the launcher always browses with its built-in
 * ImGui picker (launcher_native_file_picker_available() is false), so these
 * only have to satisfy the linker and report "cancelled". */
#include <stddef.h>
#include <stdio.h>

int tinyfd_messageBox(char const *title, char const *message,
                      char const *dialog_type, char const *icon_type,
                      int default_button) {
  (void)dialog_type; (void)icon_type; (void)default_button;
  fprintf(stderr, "[launcher] %s: %s\n", title ? title : "", message ? message : "");
  return 0;
}

char *tinyfd_saveFileDialog(char const *title, char const *def, int n,
                            char const *const *patterns, char const *desc) {
  (void)title; (void)def; (void)n; (void)patterns; (void)desc;
  return NULL;
}

char *tinyfd_openFileDialog(char const *title, char const *def, int n,
                            char const *const *patterns, char const *desc,
                            int multi) {
  (void)title; (void)def; (void)n; (void)patterns; (void)desc; (void)multi;
  return NULL;
}

char *tinyfd_selectFolderDialog(char const *title, char const *def) {
  (void)title; (void)def;
  return NULL;
}
