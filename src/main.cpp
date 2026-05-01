#include "app_window.h"
#include "chat_store.h"
#include "config.h"
#include <curl/curl.h>
#include <cstdlib>
#include <cstring>
#include <gtk/gtk.h>

namespace {

/* gtk_settings tweaks run *after* gtk_init(); RC files are parsed *during* gtk_init().
 * If ~/.gtkrc-2.0 etc. set gtk-theme-name to Adwaita but no GTK2 engine is installed,
 * GTK can crash right after printing the theme-engine warning. Replace the RC search
 * path with a tiny gtkrc unless the user already set GTK2_RC_FILES.
 * Escape hatch: KINDLE_LLM_CHAT_GTK_THEME (theme name), KINDLE_LLM_CHAT_SKIP_RC_OVERRIDE=1 */
bool install_minimal_gtk2_rc_for_init() {
  if (std::getenv("KINDLE_LLM_CHAT_SKIP_RC_OVERRIDE")) {
    return true;
  }
  const char *existing = std::getenv("GTK2_RC_FILES");
  if (existing && existing[0]) {
    return true;
  }

  const char *theme = std::getenv("KINDLE_LLM_CHAT_GTK_THEME");
  if (!theme || !theme[0]) {
    theme = "Raleigh";
  }
  std::string safe_theme = theme;
  for (auto &c : safe_theme) {
    if (c == '"' || c == '\n' || c == '\r') {
      c = '_';
    }
  }

  GError *err = nullptr;
  gchar dir_template[] = "/tmp/kindle-llm-chat-gtk-XXXXXX";
  gchar *dir = g_mkdtemp(dir_template);
  if (!dir) {
    return false;
  }
  gchar *path = g_build_filename(dir, "gtkrc-minimal", static_cast<char *>(nullptr));
  gchar *contents =
      g_strdup_printf("gtk-theme-name = \"%s\"\ngtk-icon-theme-name = \"hicolor\"\n",
                      safe_theme.c_str());
  gboolean ok =
      g_file_set_contents(path, contents, static_cast<gssize>(std::strlen(contents)), &err);
  g_free(contents);
  if (!ok) {
    if (err) {
      g_warning("%s", err->message);
      g_error_free(err);
    }
    g_free(path);
    return false;
  }
  if (g_setenv("GTK2_RC_FILES", path, TRUE) == FALSE) {
    g_warning("g_setenv(GTK2_RC_FILES) failed");
    g_free(path);
    return false;
  }
  /* `dir` points at stack buf from g_mkdtemp; RC file stays under that path until exit. */
  g_free(path);
  return true;
}

} // namespace

int main(int argc, char **argv) {
  /* libcurl global init must happen once, early, before any other library forks threads
   * (GTK may trigger resolver work). Per-easy-handle work belongs after gtk_init. */
  curl_global_init(CURL_GLOBAL_DEFAULT);

  install_minimal_gtk2_rc_for_init();
  gtk_init(&argc, &argv);

  /* Reinforce theme after init (covers late RC merges / settings). */
  GtkSettings *gtk_settings = gtk_settings_get_default();
  if (gtk_settings) {
    const char *theme = std::getenv("KINDLE_LLM_CHAT_GTK_THEME");
    if (!theme || !theme[0]) {
      theme = "Raleigh";
    }
    g_object_set(gtk_settings, "gtk-theme-name", theme, static_cast<void *>(nullptr));
  }

  const char *data_dir_env = std::getenv("KINDLE_LLM_CHAT_DATA_DIR");
  const std::string base_dir =
      data_dir_env ? data_dir_env : "/mnt/us/extensions/kindle_llm_chat/data";
  ChatStore chat_store(base_dir);
  chat_store.load();

  ConfigStore config_store(base_dir);
  config_store.load();

  AppWindow app(chat_store, config_store);
  app.show();

  gtk_main();
  curl_global_cleanup();
  return 0;
}
