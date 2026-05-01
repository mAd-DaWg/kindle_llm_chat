#pragma once

#include <string>
#include <vector>

struct ChatMessage {
  std::string role;
  std::string content;
  long timestamp;
};

struct ChatUsage {
  int prompt_tokens = 0;
  int completion_tokens = 0;
  int total_tokens = 0;
  double context_percent = 0.0;
};

struct ChatThread {
  std::string id;
  std::string title;
  std::string model;
  long created_at;
  std::vector<ChatMessage> messages;
  ChatUsage last_usage;
};

class ChatStore {
public:
  explicit ChatStore(const std::string &base_dir);

  bool load();
  bool save_all() const;

  const std::vector<ChatThread> &threads() const;
  ChatThread *find(const std::string &id);
  const ChatThread *find(const std::string &id) const;

  ChatThread &create_thread(const std::string &title);
  bool delete_thread(const std::string &id);

  bool append_message(const std::string &id, const ChatMessage &message);
  bool update_last_usage(const std::string &id, const ChatUsage &usage);
  bool update_model(const std::string &id, const std::string &model);
  bool rename_if_default_title(const std::string &id, const std::string &prompt);

  std::string data_dir() const;

private:
  std::string base_dir_;
  std::vector<ChatThread> threads_;

  std::string chats_dir() const;
  std::string index_path() const;
  static std::string make_id();
  static long now_unix();
  bool ensure_dirs() const;
  bool save_thread(const ChatThread &thread) const;
  bool load_thread(const std::string &id);
};
