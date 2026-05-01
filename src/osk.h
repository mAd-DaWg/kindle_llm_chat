#pragma once

#include <gtk/gtk.h>
#include <string>
#include <vector>

struct OskKeyVariant {
  std::string display;
  std::string action;
};

struct OskKey {
  int width = 1000;
  bool obey_caps = false;
  OskKeyVariant normal;
  OskKeyVariant shifted;
};

class OnScreenKeyboard {
public:
  OnScreenKeyboard();
  ~OnScreenKeyboard();

  bool load_layout(const std::string &path);
  GtkWidget *widget() const;
  void set_target_entry(GtkEntry *entry);
  void toggle_visible();
  bool is_visible() const;

private:
  GtkWidget *root_ = nullptr;
  GtkWidget *rows_box_ = nullptr;
  GtkEntry *target_entry_ = nullptr;
  bool shift_ = false;
  std::vector<std::vector<OskKey>> rows_;

  void rebuild();
  static void on_key_clicked(GtkWidget *button, gpointer user_data);
  void apply_key(const OskKey &key);
};
