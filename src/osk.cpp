#include "osk.h"
#include <cstdlib>
#include <cstring>

namespace {
struct ParseState {
  std::vector<std::vector<OskKey>> *rows = nullptr;
  std::vector<OskKey> *current_row = nullptr;
  OskKey *current_key = nullptr;
  std::string current_tag;
};

void start_element(GMarkupParseContext *, const gchar *name, const gchar **attr_names,
                   const gchar **attr_values, gpointer user_data, GError **) {
  ParseState *state = static_cast<ParseState *>(user_data);
  state->current_tag = name;
  if (std::strcmp(name, "row") == 0) {
    state->rows->push_back({});
    state->current_row = &state->rows->back();
  } else if (std::strcmp(name, "key") == 0 && state->current_row) {
    OskKey key;
    for (int i = 0; attr_names && attr_names[i]; ++i) {
      if (std::strcmp(attr_names[i], "width") == 0) {
        key.width = std::atoi(attr_values[i]);
      } else if (std::strcmp(attr_names[i], "obey-caps") == 0) {
        key.obey_caps = std::strcmp(attr_values[i], "true") == 0;
      }
    }
    state->current_row->push_back(key);
    state->current_key = &state->current_row->back();
  }
}

void text_element(GMarkupParseContext *, const gchar *text, gsize text_len, gpointer user_data, GError **) {
  ParseState *state = static_cast<ParseState *>(user_data);
  if (!state->current_key) {
    return;
  }
  std::string content(text, text_len);
  if (state->current_tag == "default") {
    state->current_key->normal.display = content;
    state->current_key->normal.action = content;
  } else if (state->current_tag == "shifted") {
    state->current_key->shifted.display = content;
    state->current_key->shifted.action = content;
  }
}

void end_element(GMarkupParseContext *, const gchar *name, gpointer user_data, GError **) {
  ParseState *state = static_cast<ParseState *>(user_data);
  if (std::strcmp(name, "key") == 0) {
    state->current_key = nullptr;
  } else if (std::strcmp(name, "row") == 0) {
    state->current_row = nullptr;
  }
  state->current_tag.clear();
}
} // namespace

OnScreenKeyboard::OnScreenKeyboard() {
  root_ = gtk_vbox_new(FALSE, 2);
  rows_box_ = gtk_vbox_new(FALSE, 2);
  gtk_box_pack_start(GTK_BOX(root_), rows_box_, FALSE, FALSE, 0);
}

OnScreenKeyboard::~OnScreenKeyboard() = default;

GtkWidget *OnScreenKeyboard::widget() const { return root_; }
bool OnScreenKeyboard::is_visible() const { return GTK_WIDGET_VISIBLE(root_); }

void OnScreenKeyboard::set_target_entry(GtkEntry *entry) { target_entry_ = entry; }

void OnScreenKeyboard::toggle_visible() {
  if (GTK_WIDGET_VISIBLE(root_)) {
    gtk_widget_hide(root_);
  } else {
    gtk_widget_show_all(root_);
  }
}

bool OnScreenKeyboard::load_layout(const std::string &path) {
  gchar *content = nullptr;
  gsize len = 0;
  GError *error = nullptr;
  if (!g_file_get_contents(path.c_str(), &content, &len, &error)) {
    if (error) {
      g_error_free(error);
    }
    return false;
  }

  rows_.clear();
  ParseState state;
  state.rows = &rows_;
  GMarkupParser parser = {};
  parser.start_element = start_element;
  parser.text = text_element;
  parser.end_element = end_element;
  GMarkupParseContext *ctx = g_markup_parse_context_new(&parser, G_MARKUP_TREAT_CDATA_AS_TEXT, &state, nullptr);
  g_markup_parse_context_parse(ctx, content, len, &error);
  g_markup_parse_context_end_parse(ctx, &error);
  g_markup_parse_context_free(ctx);
  g_free(content);
  if (error) {
    g_error_free(error);
    return false;
  }
  rebuild();
  return true;
}

void OnScreenKeyboard::on_key_clicked(GtkWidget *button, gpointer user_data) {
  OnScreenKeyboard *self = static_cast<OnScreenKeyboard *>(user_data);
  OskKey *key = static_cast<OskKey *>(g_object_get_data(G_OBJECT(button), "osk-key"));
  if (self && key) {
    self->apply_key(*key);
  }
}

void OnScreenKeyboard::apply_key(const OskKey &key) {
  if (!target_entry_) {
    return;
  }
  const OskKeyVariant &variant = (shift_ && !key.shifted.action.empty()) ? key.shifted : key.normal;
  const std::string &action = variant.action;
  if (action == "space") {
    const gchar *old = gtk_entry_get_text(target_entry_);
    std::string next = std::string(old ? old : "") + " ";
    gtk_entry_set_text(target_entry_, next.c_str());
  } else if (action == "backspace") {
    const gchar *old = gtk_entry_get_text(target_entry_);
    std::string next = old ? old : "";
    if (!next.empty()) {
      next.pop_back();
    }
    gtk_entry_set_text(target_entry_, next.c_str());
  } else if (action == "shift") {
    shift_ = !shift_;
    rebuild();
  } else if (!action.empty()) {
    const gchar *old = gtk_entry_get_text(target_entry_);
    std::string next = std::string(old ? old : "") + action;
    gtk_entry_set_text(target_entry_, next.c_str());
    if (shift_) {
      shift_ = false;
      rebuild();
    }
  }
}

void OnScreenKeyboard::rebuild() {
  GList *children = gtk_container_get_children(GTK_CONTAINER(rows_box_));
  for (GList *l = children; l != nullptr; l = l->next) {
    gtk_widget_destroy(GTK_WIDGET(l->data));
  }
  g_list_free(children);

  for (auto &row : rows_) {
    GtkWidget *h = gtk_hbox_new(TRUE, 2);
    for (auto &key : row) {
      const OskKeyVariant &variant = (shift_ && !key.shifted.display.empty()) ? key.shifted : key.normal;
      GtkWidget *b = gtk_button_new_with_label(variant.display.c_str());
      g_object_set_data(G_OBJECT(b), "osk-key", &key);
      g_signal_connect(b, "clicked", G_CALLBACK(OnScreenKeyboard::on_key_clicked), this);
      gtk_box_pack_start(GTK_BOX(h), b, TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(rows_box_), h, FALSE, FALSE, 0);
  }
  gtk_widget_show_all(rows_box_);
}
