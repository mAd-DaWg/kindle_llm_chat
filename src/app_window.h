#pragma once

#include "chat_store.h"
#include "config.h"
#include "http_client.h"
#include "osk.h"
#include <atomic>
#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class AppWindow {
public:
  AppWindow(ChatStore &chat_store, ConfigStore &config_store);
  ~AppWindow();
  GtkWidget *widget() const;
  void show();
  /** If `epoch` matches the active stream, clear stream state and refresh compose/stop (GTK main thread). */
  void notify_stream_terminal(uint64_t epoch);
  void append_line(const std::string &role, const std::string &content);
  void begin_assistant_stream();
  void append_assistant_token(const std::string &token);
  void end_assistant_stream();
  void refresh_usage_label(const ChatUsage &usage);
  /** Used from GTK idle callbacks (same TU); keeps stream tokens scoped to the correct chat. */
  void try_stream_token_ui(const std::string &target_chat_id, const std::string &token, uint64_t stream_gen);
  void catchup_streaming_ui();

private:
  ChatStore &chat_store_;
  ConfigStore &config_store_;
  HttpClient http_client_;
  OnScreenKeyboard osk_;
  std::string active_chat_id_;

  GtkWidget *window_ = nullptr;
  GtkWidget *chat_scroll_ = nullptr;
  /** Vertical list of message rows (bubbles); each row owns its GtkTextView buffer. */
  GtkWidget *chat_messages_box_ = nullptr;
  /** Buffer of the assistant row created for the active stream; null when not streaming. */
  GtkTextBuffer *stream_reply_buffer_ = nullptr;
  GtkWidget *message_entry_ = nullptr;
  GtkWidget *usage_label_ = nullptr;
  GtkWidget *send_button_ = nullptr;
  GtkWidget *stop_button_ = nullptr;
  GtkWidget *model_combo_ = nullptr;
  GtkWidget *chat_list_ = nullptr;
  GtkListStore *chat_list_store_ = nullptr;
  GtkWidget *new_chat_button_ = nullptr;
  GtkWidget *delete_chat_button_ = nullptr;
  GtkWidget *settings_button_ = nullptr;
  GtkWidget *keyboard_button_ = nullptr;
  GtkWidget *close_button_ = nullptr;
  bool model_combo_signal_blocked_ = false;
  std::vector<std::string> available_models_;
  std::atomic<uint64_t> next_stream_gen_{0};
  std::unordered_map<uint64_t, std::pair<std::string, std::shared_ptr<std::string>>> active_streams_;
  bool stream_ui_began_ = false;
  /** Stream epoch for which the in-view assistant bubble was opened (tokens + end must match). */
  uint64_t assistant_ui_epoch_ = 0;

  void build_ui();
  void reload_sidebar();
  void load_chat_into_view(const std::string &chat_id);
  void unregister_stream_gen(uint64_t gen);
  bool stream_gen_is_active(uint64_t gen) const;
  void scroll_chat_to_bottom();
  void clear_messages_list();
  gint bubble_width_for_layout() const;
  /** Adds a messenger-style row; returns the message GtkTextBuffer (empty `text` still creates a bubble). */
  GtkTextBuffer *add_chat_bubble(const std::string &role, const std::string &text, bool is_error);
  /** Replace streamed assistant plain text with rendered markdown. */
  void finish_assistant_markdown(const std::string &full_text);
  void refresh_usage_estimate_for_stream(const std::string &chat_id,
                                         const std::shared_ptr<std::string> &assistant_so_far);
  void maybe_bootstrap_chat();
  void refresh_models();
  void sync_model_combo_with_active_chat();
  void schedule_initial_model_fetch();
  void on_model_changed();

  static gboolean idle_initial_model_fetch(gpointer data);
  static gboolean idle_append_text(gpointer data);
  static gboolean idle_usage(gpointer data);
  static gboolean idle_scroll_chat_to_bottom(gpointer data);

  void on_new_chat();
  void on_delete_chat();
  void on_send();
  void on_stop_stream();
  /** Send / model / Stop reflect whether the visible chat is the one mid-stream. */
  void sync_streaming_controls();
  void on_select_chat();
  void on_toggle_keyboard();
  void on_show_settings();
  void on_close_app();

  static void on_new_chat_clicked(GtkWidget *, gpointer data);
  static void on_delete_chat_clicked(GtkWidget *, gpointer data);
  static void on_send_clicked(GtkWidget *, gpointer data);
  static void on_stop_clicked(GtkWidget *, gpointer data);
  static void on_settings_clicked(GtkWidget *, gpointer data);
  static void on_keyboard_clicked(GtkWidget *, gpointer data);
  static void on_close_clicked(GtkWidget *, gpointer data);
  static void on_model_combo_changed(GtkComboBox *, gpointer data);
  static void on_chat_selection_changed(GtkTreeSelection *, gpointer data);
  static void on_chat_scroll_allocate(GtkWidget *widget, GdkRectangle *allocation, gpointer data);
};
