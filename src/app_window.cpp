#include "app_window.h"
#include "markdown_view.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <sstream>

namespace {
struct UiTextEvent {
  enum class Kind { Line, AssistantToken, AssistantDone };
  AppWindow *self;
  Kind kind = Kind::Line;
  std::string role;
  std::string text;
  /** Chat id for stream token/done/error; empty for ordinary lines. */
  std::string stream_target_id;
  uint64_t stream_gen = 0;
};

struct UiUsageEvent {
  AppWindow *self;
  std::string chat_id;
  ChatUsage usage;
};


struct StreamTerminalIdle {
  AppWindow *self;
  uint64_t epoch;
};

gboolean idle_stream_terminal(gpointer data) {
  auto *d = static_cast<StreamTerminalIdle *>(data);
  d->self->notify_stream_terminal(d->epoch);
  delete d;
  return FALSE;
}
} // namespace

AppWindow::AppWindow(ChatStore &chat_store, ConfigStore &config_store)
    : chat_store_(chat_store), config_store_(config_store) {
  build_ui();
  maybe_bootstrap_chat();
  schedule_initial_model_fetch();
}

AppWindow::~AppWindow() { http_client_.cancel(); }

gboolean AppWindow::idle_append_text(gpointer data) {
  UiTextEvent *ev = static_cast<UiTextEvent *>(data);
  AppWindow *self = ev->self;
  if (ev->kind == UiTextEvent::Kind::AssistantToken) {
    if (!self->stream_gen_is_active(ev->stream_gen)) {
      delete ev;
      return FALSE;
    }
    if (ev->stream_target_id == self->active_chat_id_) {
      self->try_stream_token_ui(ev->stream_target_id, ev->text, ev->stream_gen);
    }
  } else if (ev->kind == UiTextEvent::Kind::AssistantDone) {
    if (ev->stream_gen == self->assistant_ui_epoch_ && ev->stream_target_id == self->active_chat_id_ &&
        self->stream_gen_is_active(ev->stream_gen)) {
      self->finish_assistant_markdown(ev->text);
    }
  } else {
    if (ev->role == "error" && !ev->stream_target_id.empty()) {
      if (ev->stream_target_id != self->active_chat_id_ || !self->stream_gen_is_active(ev->stream_gen)) {
        delete ev;
        return FALSE;
      }
    }
    self->append_line(ev->role, ev->text);
  }
  delete ev;
  return FALSE;
}

gboolean AppWindow::idle_usage(gpointer data) {
  UiUsageEvent *ev = static_cast<UiUsageEvent *>(data);
  /* Main thread only: persist usage per chat and refresh the meter when that chat is visible. */
  ev->self->chat_store_.update_last_usage(ev->chat_id, ev->usage);
  if (ev->chat_id == ev->self->active_chat_id_) {
    ev->self->refresh_usage_label(ev->usage);
  }
  delete ev;
  return FALSE;
}

gboolean AppWindow::idle_scroll_chat_to_bottom(gpointer data) {
  static_cast<AppWindow *>(data)->scroll_chat_to_bottom();
  return FALSE;
}

GtkWidget *AppWindow::widget() const { return window_; }
void AppWindow::show() { gtk_widget_show_all(window_); }

void AppWindow::build_ui() {
  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window_), "Kindle LLM Chat");
  gtk_window_set_default_size(GTK_WINDOW(window_), 900, 650);
  g_signal_connect(window_, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

  GtkWidget *root = gtk_vbox_new(FALSE, 4);
  gtk_container_add(GTK_CONTAINER(window_), root);

  GtkWidget *pane = gtk_hpaned_new();
  gtk_box_pack_start(GTK_BOX(root), pane, TRUE, TRUE, 0);

  GtkWidget *left = gtk_vbox_new(FALSE, 4);
  gtk_widget_set_size_request(left, 220, -1);
  gtk_paned_add1(GTK_PANED(pane), left);

  new_chat_button_ = gtk_button_new_with_label("New Chat");
  delete_chat_button_ = gtk_button_new_with_label("Delete Chat");
  settings_button_ = gtk_button_new_with_label("Settings");
  gtk_box_pack_start(GTK_BOX(left), new_chat_button_, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(left), delete_chat_button_, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(left), settings_button_, FALSE, FALSE, 0);

  chat_list_store_ = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
  chat_list_ = gtk_tree_view_new_with_model(GTK_TREE_MODEL(chat_list_store_));
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Chats", renderer, "text", 0, nullptr);
  gtk_tree_view_append_column(GTK_TREE_VIEW(chat_list_), column);
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(chat_list_));
  g_signal_connect(selection, "changed", G_CALLBACK(on_chat_selection_changed), this);

  GtkWidget *side_scroll = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(side_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(side_scroll), chat_list_);
  gtk_box_pack_start(GTK_BOX(left), side_scroll, TRUE, TRUE, 0);

  GtkWidget *right = gtk_vbox_new(FALSE, 4);
  gtk_paned_add2(GTK_PANED(pane), right);

  GtkWidget *top = gtk_hbox_new(FALSE, 4);
  GtkWidget *title = gtk_label_new("Chat");
  close_button_ = gtk_button_new_with_label("X");
  gtk_box_pack_start(GTK_BOX(top), title, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(top), close_button_, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(right), top, FALSE, FALSE, 0);

  chat_scroll_ = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chat_scroll_), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  chat_text_view_ = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chat_text_view_), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(chat_text_view_), FALSE);
  gtk_container_add(GTK_CONTAINER(chat_scroll_), chat_text_view_);
  gtk_box_pack_start(GTK_BOX(right), chat_scroll_, TRUE, TRUE, 0);
  chat_buffer_ = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_text_view_));
  markdown_ensure_tags(chat_buffer_);

  usage_label_ = gtk_label_new("Context usage: 0.0%");
  gtk_box_pack_start(GTK_BOX(right), usage_label_, FALSE, FALSE, 0);

  GtkWidget *model_row = gtk_hbox_new(FALSE, 4);
  gtk_box_pack_start(GTK_BOX(model_row), gtk_label_new("Model"), FALSE, FALSE, 0);
  model_combo_ = gtk_combo_box_text_new();
  gtk_box_pack_start(GTK_BOX(model_row), model_combo_, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(right), model_row, FALSE, FALSE, 0);
  {
    std::string fallback = config_store_.config().model;
    if (fallback.empty()) {
      fallback = "local-model";
    }
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(model_combo_), fallback.c_str());
    available_models_.push_back(fallback);
    gtk_combo_box_set_active(GTK_COMBO_BOX(model_combo_), 0);
  }

  GtkWidget *compose = gtk_hbox_new(FALSE, 4);
  message_entry_ = gtk_entry_new();
  send_button_ = gtk_button_new_with_label("Send");
  stop_button_ = gtk_button_new_with_label("Stop");
  keyboard_button_ = gtk_button_new_with_label("Keyboard");
  gtk_box_pack_start(GTK_BOX(compose), message_entry_, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(compose), keyboard_button_, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(compose), send_button_, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(compose), stop_button_, FALSE, FALSE, 0);
  gtk_widget_hide(stop_button_);
  gtk_box_pack_start(GTK_BOX(right), compose, FALSE, FALSE, 0);

  osk_.set_target_entry(GTK_ENTRY(message_entry_));
  const char *layout_path = g_getenv("KINDLE_LLM_CHAT_LAYOUT");
  if (!layout_path || !osk_.load_layout(layout_path)) {
    osk_.load_layout("kindle.pkg/layouts/keyboard-200dpi.xml");
  }
  gtk_box_pack_start(GTK_BOX(right), osk_.widget(), FALSE, FALSE, 0);
  gtk_widget_hide(osk_.widget());

  g_signal_connect(new_chat_button_, "clicked", G_CALLBACK(on_new_chat_clicked), this);
  g_signal_connect(delete_chat_button_, "clicked", G_CALLBACK(on_delete_chat_clicked), this);
  g_signal_connect(send_button_, "clicked", G_CALLBACK(on_send_clicked), this);
  g_signal_connect(stop_button_, "clicked", G_CALLBACK(on_stop_clicked), this);
  g_signal_connect(settings_button_, "clicked", G_CALLBACK(on_settings_clicked), this);
  g_signal_connect(keyboard_button_, "clicked", G_CALLBACK(on_keyboard_clicked), this);
  g_signal_connect(close_button_, "clicked", G_CALLBACK(on_close_clicked), this);
  g_signal_connect(model_combo_, "changed", G_CALLBACK(on_model_combo_changed), this);
  reload_sidebar();
}

void AppWindow::reload_sidebar() {
  gtk_list_store_clear(chat_list_store_);
  for (const auto &thread : chat_store_.threads()) {
    GtkTreeIter it;
    gtk_list_store_append(chat_list_store_, &it);
    gtk_list_store_set(chat_list_store_, &it, 0, thread.title.c_str(), 1, thread.id.c_str(), -1);
  }
}

void AppWindow::append_line(const std::string &role, const std::string &content) {
  markdown_ensure_tags(chat_buffer_);
  GtkTextIter end;
  gtk_text_buffer_get_end_iter(chat_buffer_, &end);
  const std::string prefix = role + ": ";
  gtk_text_buffer_insert_with_tags_by_name(chat_buffer_, &end, prefix.c_str(), -1, "md_role", nullptr);
  if (role == "error") {
    gtk_text_buffer_insert(chat_buffer_, &end, content.c_str(), -1);
  } else {
    markdown_insert(chat_buffer_, &end, content);
  }
  gtk_text_buffer_insert(chat_buffer_, &end, "\n\n", -1);
}

void AppWindow::begin_assistant_stream() {
  gtk_text_buffer_delete_mark_by_name(chat_buffer_, "assistant_body_start");
  GtkTextIter end;
  gtk_text_buffer_get_end_iter(chat_buffer_, &end);
  gtk_text_buffer_insert(chat_buffer_, &end, "assistant: ", -1);
  gtk_text_buffer_create_mark(chat_buffer_, "assistant_body_start", &end, TRUE);
}

void AppWindow::append_assistant_token(const std::string &token) {
  GtkTextIter end;
  gtk_text_buffer_get_end_iter(chat_buffer_, &end);
  gtk_text_buffer_insert(chat_buffer_, &end, token.c_str(), -1);
}

void AppWindow::end_assistant_stream() {
  GtkTextIter end;
  gtk_text_buffer_get_end_iter(chat_buffer_, &end);
  gtk_text_buffer_insert(chat_buffer_, &end, "\n\n", -1);
}

void AppWindow::finish_assistant_markdown(const std::string &full_text) {
  GtkTextMark *m = gtk_text_buffer_get_mark(chat_buffer_, "assistant_body_start");
  if (!m) {
    return;
  }
  GtkTextIter ins;
  GtkTextIter end;
  gtk_text_buffer_get_iter_at_mark(chat_buffer_, &ins, m);
  gtk_text_buffer_get_end_iter(chat_buffer_, &end);
  gtk_text_buffer_delete(chat_buffer_, &ins, &end);
  gtk_text_buffer_get_iter_at_mark(chat_buffer_, &ins, m);
  markdown_insert(chat_buffer_, &ins, full_text);
  gtk_text_buffer_delete_mark_by_name(chat_buffer_, "assistant_body_start");
  GtkTextIter tail;
  gtk_text_buffer_get_end_iter(chat_buffer_, &tail);
  gtk_text_buffer_insert(chat_buffer_, &tail, "\n\n", -1);
}

void AppWindow::refresh_usage_label(const ChatUsage &usage) {
  std::stringstream ss;
  ss.setf(std::ios::fixed);
  ss.precision(1);
  ss << "Context usage: " << usage.context_percent << "% (" << usage.total_tokens << " tokens)";
  gtk_label_set_text(GTK_LABEL(usage_label_), ss.str().c_str());
}

void AppWindow::refresh_usage_estimate_for_stream(const std::string &chat_id,
                                                  const std::shared_ptr<std::string> &assistant_so_far) {
  if (active_chat_id_ != chat_id || !assistant_so_far) {
    return;
  }
  const int ctx_window = config_store_.config().context_window;
  if (ctx_window <= 0) {
    return;
  }
  const ChatThread *t = chat_store_.find(chat_id);
  if (!t) {
    return;
  }
  size_t prompt_chars = 0;
  for (const auto &m : t->messages) {
    prompt_chars += m.content.size();
  }
  const size_t completion_chars = assistant_so_far->size();
  /* ~4 characters per token is a coarse live heuristic while bytes stream in. */
  const int prompt_tokens = static_cast<int>(prompt_chars / 4) + static_cast<int>(prompt_chars > 0);
  const int completion_tokens =
      static_cast<int>(completion_chars / 4) + static_cast<int>(completion_chars > 0);
  const int heur_total = prompt_tokens + completion_tokens;
  /* last_usage is accurate but frozen until the next on_usage; char/4 on the full transcript is far smaller. */
  const int baseline_total = std::max(0, t->last_usage.total_tokens);
  /* Tokens for the latest user message (not yet in baseline). Streaming assistant is only in assistant_so_far. */
  int pending_user_est = 0;
  if (!t->messages.empty() && t->messages.back().role == "user") {
    const size_t uz = t->messages.back().content.size();
    pending_user_est = static_cast<int>(uz / 4) + static_cast<int>(uz > 0);
  }
  const int floored_live = baseline_total + pending_user_est + completion_tokens;
  ChatUsage u;
  u.prompt_tokens = prompt_tokens;
  u.completion_tokens = completion_tokens;
  u.total_tokens = std::max(heur_total, floored_live);
  u.context_percent =
      std::min(100.0, (static_cast<double>(u.total_tokens) / static_cast<double>(ctx_window)) * 100.0);
  refresh_usage_label(u);
}

void AppWindow::maybe_bootstrap_chat() {
  if (chat_store_.threads().empty()) {
    auto &thread = chat_store_.create_thread("New Chat");
    thread.model = config_store_.config().model;
    chat_store_.save_all();
    active_chat_id_ = thread.id;
  } else {
    active_chat_id_ = chat_store_.threads().front().id;
  }
  reload_sidebar();
  load_chat_into_view(active_chat_id_);
}

void AppWindow::load_chat_into_view(const std::string &chat_id) {
  stream_ui_began_ = false;
  active_chat_id_ = chat_id;
  gtk_text_buffer_set_text(chat_buffer_, "", -1);
  const ChatThread *thread = chat_store_.find(chat_id);
  if (!thread) {
    sync_streaming_controls();
    g_idle_add(&AppWindow::idle_scroll_chat_to_bottom, this);
    return;
  }
  for (const auto &m : thread->messages) {
    append_line(m.role, m.content);
  }
  refresh_usage_label(thread->last_usage);
  sync_model_combo_with_active_chat();
  bool live_for_chat = false;
  for (const auto &kv : active_streams_) {
    if (kv.second.first == chat_id) {
      live_for_chat = true;
      break;
    }
  }
  if (live_for_chat) {
    catchup_streaming_ui();
  }
  sync_streaming_controls();
  /* Defer until layout knows new buffer height (GTK2 vadjustment upper). */
  g_idle_add(&AppWindow::idle_scroll_chat_to_bottom, this);
}

void AppWindow::on_new_chat() {
  auto &thread = chat_store_.create_thread("New Chat");
  thread.model = config_store_.config().model;
  chat_store_.save_all();
  active_chat_id_ = thread.id;
  reload_sidebar();
  load_chat_into_view(active_chat_id_);
}

void AppWindow::on_delete_chat() {
  if (active_chat_id_.empty()) {
    return;
  }
  GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window_), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
                                             GTK_BUTTONS_OK_CANCEL, "Delete current chat?");
  gint response = gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  if (response != GTK_RESPONSE_OK) {
    return;
  }
  chat_store_.delete_thread(active_chat_id_);
  if (chat_store_.threads().empty()) {
    on_new_chat();
  } else {
    load_chat_into_view(chat_store_.threads().front().id);
    reload_sidebar();
  }
}

void AppWindow::on_show_settings() {
  GtkWidget *dialog = gtk_dialog_new_with_buttons("Settings", GTK_WINDOW(window_), GTK_DIALOG_MODAL,
                                                  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_SAVE,
                                                  GTK_RESPONSE_OK, nullptr);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_table_new(6, 2, FALSE);
  gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 4);

  AppConfig cfg = config_store_.config();
  GtkWidget *url_entry = gtk_entry_new();
  GtkWidget *model_combo = gtk_combo_box_text_new();
  GtkWidget *key_entry = gtk_entry_new();
  GtkWidget *backend_entry = gtk_entry_new();
  GtkWidget *ctx_entry = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(url_entry), cfg.base_url.c_str());
  gtk_entry_set_text(GTK_ENTRY(key_entry), cfg.api_key.c_str());
  gtk_entry_set_text(GTK_ENTRY(backend_entry), cfg.backend_mode.c_str());
  gtk_entry_set_text(GTK_ENTRY(ctx_entry), std::to_string(cfg.context_window).c_str());

  {
    std::vector<std::string> server_models;
    std::string fetch_err;
    if (!http_client_.fetch_models(cfg, server_models, fetch_err)) {
      server_models.clear();
      const std::string fallback = cfg.model.empty() ? "local-model" : cfg.model;
      server_models.push_back(fallback);
    }
    for (const auto &name : server_models) {
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(model_combo), name.c_str());
    }
    gint active = 0;
    bool found = false;
    for (guint i = 0; i < server_models.size(); ++i) {
      if (server_models[i] == cfg.model) {
        active = static_cast<gint>(i);
        found = true;
        break;
      }
    }
    if (!found && !cfg.model.empty()) {
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(model_combo), cfg.model.c_str());
      active = static_cast<gint>(server_models.size());
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(model_combo), active);
  }

  gtk_table_attach(GTK_TABLE(grid), gtk_label_new("Base URL"), 0, 1, 0, 1, GTK_FILL, GTK_FILL, 2, 2);
  gtk_table_attach(GTK_TABLE(grid), url_entry, 1, 2, 0, 1,
                   static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 2, 2);
  gtk_table_attach(GTK_TABLE(grid), gtk_label_new("Model"), 0, 1, 1, 2, GTK_FILL, GTK_FILL, 2, 2);
  gtk_table_attach(GTK_TABLE(grid), model_combo, 1, 2, 1, 2,
                   static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 2, 2);
  gtk_table_attach(GTK_TABLE(grid), gtk_label_new("API Key"), 0, 1, 2, 3, GTK_FILL, GTK_FILL, 2, 2);
  gtk_table_attach(GTK_TABLE(grid), key_entry, 1, 2, 2, 3,
                   static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 2, 2);
  gtk_table_attach(GTK_TABLE(grid), gtk_label_new("Backend"), 0, 1, 3, 4, GTK_FILL, GTK_FILL, 2, 2);
  gtk_table_attach(GTK_TABLE(grid), backend_entry, 1, 2, 3, 4,
                   static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 2, 2);
  gtk_table_attach(GTK_TABLE(grid), gtk_label_new("Context Window"), 0, 1, 4, 5, GTK_FILL, GTK_FILL, 2, 2);
  gtk_table_attach(GTK_TABLE(grid), ctx_entry, 1, 2, 4, 5,
                   static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 2, 2);

  gtk_widget_show_all(dialog);
  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
    AppConfig &mutable_cfg = config_store_.mutable_config();
    mutable_cfg.base_url = gtk_entry_get_text(GTK_ENTRY(url_entry));
    gchar *model_text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(model_combo));
    mutable_cfg.model = model_text ? model_text : cfg.model;
    if (model_text) {
      g_free(model_text);
    }
    mutable_cfg.api_key = gtk_entry_get_text(GTK_ENTRY(key_entry));
    mutable_cfg.backend_mode = gtk_entry_get_text(GTK_ENTRY(backend_entry));
    mutable_cfg.context_window = std::atoi(gtk_entry_get_text(GTK_ENTRY(ctx_entry)));
    config_store_.save();
    refresh_models();
    sync_model_combo_with_active_chat();
  }
  gtk_widget_destroy(dialog);
}

void AppWindow::sync_streaming_controls() {
  bool viewing_streaming_chat = false;
  for (const auto &kv : active_streams_) {
    if (kv.second.first == active_chat_id_) {
      viewing_streaming_chat = true;
      break;
    }
  }
  const bool lock_compose = viewing_streaming_chat;

  if (send_button_) {
    gtk_widget_set_sensitive(send_button_, !lock_compose);
  }
  if (message_entry_) {
    gtk_widget_set_sensitive(message_entry_, !lock_compose);
  }
  if (model_combo_) {
    gtk_widget_set_sensitive(model_combo_, !lock_compose);
  }
  if (stop_button_) {
    if (!active_streams_.empty()) {
      gtk_widget_show(stop_button_);
    } else {
      gtk_widget_hide(stop_button_);
    }
  }
}

void AppWindow::on_stop_stream() { http_client_.cancel(); }

void AppWindow::notify_stream_terminal(uint64_t epoch) {
  unregister_stream_gen(epoch);
  sync_streaming_controls();
}

void AppWindow::unregister_stream_gen(uint64_t gen) { active_streams_.erase(gen); }

void AppWindow::scroll_chat_to_bottom() {
  if (!chat_scroll_) {
    return;
  }
  GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(chat_scroll_));
  const gdouble upper = gtk_adjustment_get_upper(adj);
  const gdouble page = gtk_adjustment_get_page_size(adj);
  gtk_adjustment_set_value(adj, std::max(0.0, upper - page));
}

bool AppWindow::stream_gen_is_active(uint64_t gen) const {
  return active_streams_.find(gen) != active_streams_.end();
}

void AppWindow::try_stream_token_ui(const std::string &target_chat_id, const std::string &token,
                                    uint64_t stream_gen) {
  if (target_chat_id != active_chat_id_) {
    return;
  }
  const auto it = active_streams_.find(stream_gen);
  if (it == active_streams_.end() || it->second.first != target_chat_id) {
    return;
  }
  /* New stream epoch while an older assistant tail is still open (Done idle not run yet). */
  if (stream_ui_began_ && stream_gen != assistant_ui_epoch_) {
    end_assistant_stream();
    stream_ui_began_ = false;
  }
  if (!stream_ui_began_) {
    begin_assistant_stream();
    stream_ui_began_ = true;
    assistant_ui_epoch_ = stream_gen;
  }
  append_assistant_token(token);
  refresh_usage_estimate_for_stream(target_chat_id, it->second.second);
}

void AppWindow::catchup_streaming_ui() {
  if (stream_ui_began_) {
    return;
  }
  uint64_t gen = 0;
  std::shared_ptr<std::string> buf;
  for (const auto &kv : active_streams_) {
    if (kv.second.first == active_chat_id_) {
      gen = kv.first;
      buf = kv.second.second;
      break;
    }
  }
  if (!buf || buf->empty()) {
    return;
  }
  begin_assistant_stream();
  append_assistant_token(*buf);
  stream_ui_began_ = true;
  assistant_ui_epoch_ = gen;
  refresh_usage_estimate_for_stream(active_chat_id_, buf);
}

void AppWindow::on_send() {
  if (active_chat_id_.empty()) {
    return;
  }
  const char *text = gtk_entry_get_text(GTK_ENTRY(message_entry_));
  if (!text || std::string(text).empty()) {
    return;
  }
  std::string prompt = text;
  gtk_entry_set_text(GTK_ENTRY(message_entry_), "");

  const std::string chat_id = active_chat_id_;
  chat_store_.append_message(active_chat_id_, {"user", prompt, static_cast<long>(std::time(nullptr))});
  chat_store_.rename_if_default_title(active_chat_id_, prompt);
  reload_sidebar();
  append_line("user", prompt);
  const ChatThread *thread = chat_store_.find(chat_id);
  std::vector<RequestMessage> request_messages;
  if (thread) {
    for (const auto &m : thread->messages) {
      request_messages.push_back({m.role, m.content});
    }
  }
  AppConfig chat_config = config_store_.config();
  if (thread && !thread->model.empty()) {
    chat_config.model = thread->model;
  }

  const uint64_t stream_gen = ++next_stream_gen_;
  stream_ui_began_ = false;

  auto assistant_buffer = std::make_shared<std::string>();
  active_streams_[stream_gen] = {chat_id, assistant_buffer};
  refresh_usage_estimate_for_stream(chat_id, assistant_buffer);
  StreamCallbacks callbacks;
  callbacks.on_token = [this, assistant_buffer, chat_id, stream_gen](const std::string &token) {
    assistant_buffer->append(token);
    UiTextEvent *ev = new UiTextEvent{};
    ev->self = this;
    ev->kind = UiTextEvent::Kind::AssistantToken;
    ev->text = token;
    ev->stream_target_id = chat_id;
    ev->stream_gen = stream_gen;
    g_idle_add(&AppWindow::idle_append_text, ev);
  };
  callbacks.on_usage = [this, chat_id](const ChatUsage &usage) {
    UiUsageEvent *ev = new UiUsageEvent{this, chat_id, usage};
    g_idle_add(&AppWindow::idle_usage, ev);
  };
  callbacks.on_done = [this, chat_id, assistant_buffer, stream_gen]() {
    if (!assistant_buffer->empty()) {
      ChatMessage assistant_message;
      assistant_message.role = "assistant";
      assistant_message.content = *assistant_buffer;
      assistant_message.timestamp = static_cast<long>(std::time(nullptr));
      chat_store_.append_message(chat_id, assistant_message);
    }
    UiTextEvent *ev = new UiTextEvent{};
    ev->self = this;
    ev->kind = UiTextEvent::Kind::AssistantDone;
    ev->text = *assistant_buffer;
    ev->stream_target_id = chat_id;
    ev->stream_gen = stream_gen;
    g_idle_add(&AppWindow::idle_append_text, ev);
    auto *st = new StreamTerminalIdle{this, stream_gen};
    g_idle_add(idle_stream_terminal, st);
  };
  callbacks.on_error = [this, chat_id, stream_gen](const std::string &error) {
    UiTextEvent *ev = new UiTextEvent{};
    ev->self = this;
    ev->kind = UiTextEvent::Kind::Line;
    ev->role = "error";
    ev->text = error;
    ev->stream_target_id = chat_id;
    ev->stream_gen = stream_gen;
    g_idle_add(&AppWindow::idle_append_text, ev);
    auto *st = new StreamTerminalIdle{this, stream_gen};
    g_idle_add(idle_stream_terminal, st);
  };

  sync_streaming_controls();
  http_client_.stream_chat(chat_config, request_messages, callbacks);
}

void AppWindow::on_select_chat() {
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(chat_list_));
  GtkTreeModel *model = nullptr;
  GtkTreeIter iter;
  if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
    gchar *chat_id = nullptr;
    gtk_tree_model_get(model, &iter, 1, &chat_id, -1);
    if (chat_id) {
      load_chat_into_view(chat_id);
      g_free(chat_id);
    }
  }
}

void AppWindow::on_toggle_keyboard() { osk_.toggle_visible(); }
void AppWindow::on_close_app() { gtk_main_quit(); }
void AppWindow::on_model_changed() {
  if (model_combo_signal_blocked_ || active_chat_id_.empty()) {
    return;
  }
  gchar *model = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(model_combo_));
  if (!model) {
    return;
  }
  chat_store_.update_model(active_chat_id_, model);
  g_free(model);
}

void AppWindow::schedule_initial_model_fetch() {
  g_idle_add(&AppWindow::idle_initial_model_fetch, this);
}

gboolean AppWindow::idle_initial_model_fetch(gpointer data) {
  auto *self = static_cast<AppWindow *>(data);
  self->refresh_models();
  self->sync_model_combo_with_active_chat();
  return FALSE;
}

void AppWindow::refresh_models() {
  if (!model_combo_) {
    return;
  }
  available_models_.clear();
  std::string error;
  std::vector<std::string> fetched;
  if (!http_client_.fetch_models(config_store_.config(), fetched, error)) {
    fetched.push_back(config_store_.config().model);
  }
  available_models_ = fetched;

  model_combo_signal_blocked_ = true;
  GtkTreeModel *combo_model = gtk_combo_box_get_model(GTK_COMBO_BOX(model_combo_));
  if (combo_model) {
    gint rows = gtk_tree_model_iter_n_children(combo_model, nullptr);
    while (rows > 0) {
      gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(model_combo_), 0);
      rows = gtk_tree_model_iter_n_children(combo_model, nullptr);
    }
  }
  for (const auto &model_name : available_models_) {
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(model_combo_), model_name.c_str());
  }
  if (!available_models_.empty()) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(model_combo_), 0);
  }
  model_combo_signal_blocked_ = false;
}

void AppWindow::sync_model_combo_with_active_chat() {
  const ChatThread *thread = chat_store_.find(active_chat_id_);
  if (!thread || !model_combo_) {
    return;
  }
  std::string wanted = thread->model.empty() ? config_store_.config().model : thread->model;

  bool found = false;
  for (guint i = 0; i < available_models_.size(); ++i) {
    if (available_models_[i] == wanted) {
      model_combo_signal_blocked_ = true;
      gtk_combo_box_set_active(GTK_COMBO_BOX(model_combo_), static_cast<gint>(i));
      model_combo_signal_blocked_ = false;
      found = true;
      break;
    }
  }
  if (!found) {
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(model_combo_), wanted.c_str());
    available_models_.push_back(wanted);
    model_combo_signal_blocked_ = true;
    gtk_combo_box_set_active(GTK_COMBO_BOX(model_combo_), static_cast<gint>(available_models_.size() - 1));
    model_combo_signal_blocked_ = false;
  }
}

void AppWindow::on_new_chat_clicked(GtkWidget *, gpointer data) { static_cast<AppWindow *>(data)->on_new_chat(); }
void AppWindow::on_delete_chat_clicked(GtkWidget *, gpointer data) { static_cast<AppWindow *>(data)->on_delete_chat(); }
void AppWindow::on_send_clicked(GtkWidget *, gpointer data) { static_cast<AppWindow *>(data)->on_send(); }
void AppWindow::on_stop_clicked(GtkWidget *, gpointer data) { static_cast<AppWindow *>(data)->on_stop_stream(); }
void AppWindow::on_settings_clicked(GtkWidget *, gpointer data) { static_cast<AppWindow *>(data)->on_show_settings(); }
void AppWindow::on_keyboard_clicked(GtkWidget *, gpointer data) { static_cast<AppWindow *>(data)->on_toggle_keyboard(); }
void AppWindow::on_close_clicked(GtkWidget *, gpointer data) { static_cast<AppWindow *>(data)->on_close_app(); }
void AppWindow::on_model_combo_changed(GtkComboBox *, gpointer data) {
  static_cast<AppWindow *>(data)->on_model_changed();
}
void AppWindow::on_chat_selection_changed(GtkTreeSelection *, gpointer data) {
  static_cast<AppWindow *>(data)->on_select_chat();
}
