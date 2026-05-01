#include "stream_parser.h"
#include <algorithm>
#include <json-glib/json-glib.h>

StreamParser::StreamParser(const AppConfig &config) : config_(config) {}

void StreamParser::update_context_percent(ChatUsage &usage) const {
  if (config_.context_window <= 0) {
    usage.context_percent = 0.0;
    return;
  }
  usage.context_percent = (static_cast<double>(usage.total_tokens) /
                           static_cast<double>(config_.context_window)) * 100.0;
  usage.context_percent = std::clamp(usage.context_percent, 0.0, 100.0);
}

void StreamParser::parse_openai_chunk(const std::string &json_line, const StreamCallbacks &callbacks) {
  JsonParser *parser = json_parser_new();
  GError *error = nullptr;
  if (!json_parser_load_from_data(parser, json_line.c_str(), json_line.size(), &error)) {
    if (error) {
      g_error_free(error);
    }
    g_object_unref(parser);
    return;
  }

  JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
  if (json_object_has_member(obj, "choices")) {
    JsonArray *choices = json_object_get_array_member(obj, "choices");
    if (choices && json_array_get_length(choices) > 0) {
      JsonObject *choice = json_array_get_object_element(choices, 0);
      if (json_object_has_member(choice, "delta")) {
        JsonObject *delta = json_object_get_object_member(choice, "delta");
        if (json_object_has_member(delta, "content")) {
          const char *token = json_object_get_string_member(delta, "content");
          if (token && callbacks.on_token) {
            callbacks.on_token(token);
          }
        }
      }
    }
  }
  if (json_object_has_member(obj, "usage")) {
    JsonNode *usage_node = json_object_get_member(obj, "usage");
    if (usage_node && json_node_get_node_type(usage_node) == JSON_NODE_OBJECT) {
      JsonObject *usage = json_object_get_object_member(obj, "usage");
      if (json_object_has_member(usage, "prompt_tokens")) {
        usage_.prompt_tokens = static_cast<int>(json_object_get_int_member(usage, "prompt_tokens"));
      }
      if (json_object_has_member(usage, "completion_tokens")) {
        usage_.completion_tokens = static_cast<int>(json_object_get_int_member(usage, "completion_tokens"));
      }
      if (json_object_has_member(usage, "total_tokens")) {
        usage_.total_tokens = static_cast<int>(json_object_get_int_member(usage, "total_tokens"));
      } else {
        usage_.total_tokens = usage_.prompt_tokens + usage_.completion_tokens;
      }
      update_context_percent(usage_);
      if (callbacks.on_usage &&
          (usage_.total_tokens > 0 || usage_.prompt_tokens > 0 || usage_.completion_tokens > 0)) {
        callbacks.on_usage(usage_);
      }
    }
  }
  g_object_unref(parser);
}

void StreamParser::parse_ollama_chunk(const std::string &json_line, const StreamCallbacks &callbacks) {
  JsonParser *parser = json_parser_new();
  GError *error = nullptr;
  if (!json_parser_load_from_data(parser, json_line.c_str(), json_line.size(), &error)) {
    if (error) {
      g_error_free(error);
    }
    g_object_unref(parser);
    return;
  }
  JsonObject *obj = json_node_get_object(json_parser_get_root(parser));

  if (json_object_has_member(obj, "message")) {
    JsonObject *message = json_object_get_object_member(obj, "message");
    if (message && json_object_has_member(message, "content") && callbacks.on_token) {
      const char *token = json_object_get_string_member(message, "content");
      callbacks.on_token(token ? token : "");
    }
  } else if (json_object_has_member(obj, "response") && callbacks.on_token) {
    const char *token = json_object_get_string_member(obj, "response");
    callbacks.on_token(token ? token : "");
  }

  if (json_object_has_member(obj, "prompt_eval_count")) {
    usage_.prompt_tokens = json_object_get_int_member(obj, "prompt_eval_count");
  }
  if (json_object_has_member(obj, "eval_count")) {
    usage_.completion_tokens = json_object_get_int_member(obj, "eval_count");
  }
  usage_.total_tokens = usage_.prompt_tokens + usage_.completion_tokens;
  update_context_percent(usage_);
  if ((usage_.total_tokens > 0) && callbacks.on_usage) {
    callbacks.on_usage(usage_);
  }

  if (json_object_has_member(obj, "done") && json_object_get_boolean_member(obj, "done")) {
    stream_done_ = true;
    if (callbacks.on_done) {
      callbacks.on_done();
    }
  }
  g_object_unref(parser);
}

void StreamParser::parse_line(const std::string &line, const StreamCallbacks &callbacks) {
  if (line.empty()) {
    return;
  }
  std::string payload = line;
  if (payload.rfind("data:", 0) == 0) {
    payload = payload.substr(5);
    while (!payload.empty() && payload.front() == ' ') {
      payload.erase(payload.begin());
    }
  }
  if (payload == "[DONE]") {
    stream_done_ = true;
    if (callbacks.on_done) {
      callbacks.on_done();
    }
    return;
  }
  if (config_.backend_mode == "ollama") {
    parse_ollama_chunk(payload, callbacks);
  } else {
    parse_openai_chunk(payload, callbacks);
  }
}

void StreamParser::feed(const char *data, size_t len, const StreamCallbacks &callbacks) {
  buffer_.append(data, len);
  size_t pos = std::string::npos;
  while ((pos = buffer_.find('\n')) != std::string::npos) {
    std::string line = buffer_.substr(0, pos);
    buffer_.erase(0, pos + 1);
    parse_line(line, callbacks);
  }
}

void StreamParser::finish(const StreamCallbacks &callbacks) {
  if (!buffer_.empty()) {
    parse_line(buffer_, callbacks);
    buffer_.clear();
  }
  if (!stream_done_ && callbacks.on_done) {
    callbacks.on_done();
  }
}
