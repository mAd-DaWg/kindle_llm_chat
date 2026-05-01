#pragma once

#include "chat_store.h"
#include "config.h"
#include <functional>
#include <string>

struct StreamCallbacks {
  std::function<void(const std::string &)> on_token;
  std::function<void(const ChatUsage &)> on_usage;
  std::function<void()> on_done;
  std::function<void(const std::string &)> on_error;
};

class StreamParser {
public:
  explicit StreamParser(const AppConfig &config);
  void feed(const char *data, size_t len, const StreamCallbacks &callbacks);
  void finish(const StreamCallbacks &callbacks);

private:
  AppConfig config_;
  std::string buffer_;
  bool stream_done_ = false;
  ChatUsage usage_;

  void parse_line(const std::string &line, const StreamCallbacks &callbacks);
  void parse_openai_chunk(const std::string &json_line, const StreamCallbacks &callbacks);
  void parse_ollama_chunk(const std::string &json_line, const StreamCallbacks &callbacks);
  void update_context_percent(ChatUsage &usage) const;
};
