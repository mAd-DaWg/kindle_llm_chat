#include "chat_store.h"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <json-glib/json-glib.h>
#include <sstream>

namespace fs = std::filesystem;

ChatStore::ChatStore(const std::string &base_dir) : base_dir_(base_dir) {}

bool ChatStore::ensure_dirs() const {
  std::error_code ec;
  fs::create_directories(chats_dir(), ec);
  return !ec;
}

std::string ChatStore::chats_dir() const { return base_dir_ + "/chats"; }
std::string ChatStore::index_path() const { return base_dir_ + "/index.json"; }
std::string ChatStore::data_dir() const { return base_dir_; }
const std::vector<ChatThread> &ChatStore::threads() const { return threads_; }

long ChatStore::now_unix() { return static_cast<long>(std::time(nullptr)); }

std::string ChatStore::make_id() {
  std::stringstream ss;
  ss << now_unix() << "-" << g_random_int();
  return ss.str();
}

bool ChatStore::load() {
  threads_.clear();
  ensure_dirs();
  if (!fs::exists(index_path())) {
    return true;
  }

  JsonParser *parser = json_parser_new();
  GError *error = nullptr;
  if (!json_parser_load_from_file(parser, index_path().c_str(), &error)) {
    if (error) {
      g_error_free(error);
    }
    g_object_unref(parser);
    return false;
  }

  JsonNode *root = json_parser_get_root(parser);
  JsonArray *arr = json_node_get_array(root);
  const guint n = json_array_get_length(arr);
  for (guint i = 0; i < n; ++i) {
    JsonObject *obj = json_array_get_object_element(arr, i);
    const char *id = json_object_get_string_member(obj, "id");
    if (id) {
      load_thread(id);
    }
  }
  g_object_unref(parser);
  return true;
}

bool ChatStore::save_all() const {
  ensure_dirs();
  JsonBuilder *builder = json_builder_new();
  json_builder_begin_array(builder);
  for (const auto &thread : threads_) {
    save_thread(thread);
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "id");
    json_builder_add_string_value(builder, thread.id.c_str());
    json_builder_end_object(builder);
  }
  json_builder_end_array(builder);

  JsonGenerator *gen = json_generator_new();
  JsonNode *node = json_builder_get_root(builder);
  json_generator_set_root(gen, node);
  GError *error = nullptr;
  json_generator_to_file(gen, index_path().c_str(), &error);
  if (error) {
    g_error_free(error);
  }
  json_node_free(node);
  g_object_unref(gen);
  g_object_unref(builder);
  return error == nullptr;
}

bool ChatStore::save_thread(const ChatThread &thread) const {
  JsonBuilder *builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "id");
  json_builder_add_string_value(builder, thread.id.c_str());
  json_builder_set_member_name(builder, "title");
  json_builder_add_string_value(builder, thread.title.c_str());
  json_builder_set_member_name(builder, "model");
  json_builder_add_string_value(builder, thread.model.c_str());
  json_builder_set_member_name(builder, "created_at");
  json_builder_add_int_value(builder, thread.created_at);

  json_builder_set_member_name(builder, "messages");
  json_builder_begin_array(builder);
  for (const auto &message : thread.messages) {
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "role");
    json_builder_add_string_value(builder, message.role.c_str());
    json_builder_set_member_name(builder, "content");
    json_builder_add_string_value(builder, message.content.c_str());
    json_builder_set_member_name(builder, "timestamp");
    json_builder_add_int_value(builder, message.timestamp);
    json_builder_end_object(builder);
  }
  json_builder_end_array(builder);

  json_builder_set_member_name(builder, "last_usage");
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "prompt_tokens");
  json_builder_add_int_value(builder, thread.last_usage.prompt_tokens);
  json_builder_set_member_name(builder, "completion_tokens");
  json_builder_add_int_value(builder, thread.last_usage.completion_tokens);
  json_builder_set_member_name(builder, "total_tokens");
  json_builder_add_int_value(builder, thread.last_usage.total_tokens);
  json_builder_set_member_name(builder, "context_percent");
  json_builder_add_double_value(builder, thread.last_usage.context_percent);
  json_builder_end_object(builder);

  json_builder_end_object(builder);

  std::string path = chats_dir() + "/" + thread.id + ".json";
  JsonGenerator *gen = json_generator_new();
  JsonNode *node = json_builder_get_root(builder);
  json_generator_set_root(gen, node);
  GError *error = nullptr;
  json_generator_to_file(gen, path.c_str(), &error);
  if (error) {
    g_error_free(error);
  }
  json_node_free(node);
  g_object_unref(gen);
  g_object_unref(builder);
  return error == nullptr;
}

bool ChatStore::load_thread(const std::string &id) {
  const std::string path = chats_dir() + "/" + id + ".json";
  if (!fs::exists(path)) {
    return false;
  }
  JsonParser *parser = json_parser_new();
  GError *error = nullptr;
  if (!json_parser_load_from_file(parser, path.c_str(), &error)) {
    if (error) {
      g_error_free(error);
    }
    g_object_unref(parser);
    return false;
  }
  JsonNode *root = json_parser_get_root(parser);
  JsonObject *obj = json_node_get_object(root);
  ChatThread thread;
  thread.id = json_object_get_string_member(obj, "id");
  thread.title = json_object_get_string_member(obj, "title");
  if (json_object_has_member(obj, "model")) {
    thread.model = json_object_get_string_member(obj, "model");
  }
  thread.created_at = json_object_get_int_member(obj, "created_at");

  JsonArray *messages = json_object_get_array_member(obj, "messages");
  if (messages) {
    const guint n = json_array_get_length(messages);
    for (guint i = 0; i < n; ++i) {
      JsonObject *m = json_array_get_object_element(messages, i);
      ChatMessage msg;
      msg.role = json_object_get_string_member(m, "role");
      msg.content = json_object_get_string_member(m, "content");
      msg.timestamp = json_object_get_int_member(m, "timestamp");
      thread.messages.push_back(msg);
    }
  }
  if (json_object_has_member(obj, "last_usage")) {
    JsonObject *u = json_object_get_object_member(obj, "last_usage");
    thread.last_usage.prompt_tokens = json_object_get_int_member(u, "prompt_tokens");
    thread.last_usage.completion_tokens = json_object_get_int_member(u, "completion_tokens");
    thread.last_usage.total_tokens = json_object_get_int_member(u, "total_tokens");
    thread.last_usage.context_percent = json_object_get_double_member(u, "context_percent");
  }
  threads_.push_back(thread);
  g_object_unref(parser);
  return true;
}

ChatThread &ChatStore::create_thread(const std::string &title) {
  ChatThread thread;
  thread.id = make_id();
  thread.title = title;
  thread.created_at = now_unix();
  threads_.push_back(thread);
  save_all();
  return threads_.back();
}

bool ChatStore::delete_thread(const std::string &id) {
  for (auto it = threads_.begin(); it != threads_.end(); ++it) {
    if (it->id == id) {
      threads_.erase(it);
      std::error_code ec;
      fs::remove(chats_dir() + "/" + id + ".json", ec);
      save_all();
      return true;
    }
  }
  return false;
}

ChatThread *ChatStore::find(const std::string &id) {
  for (auto &thread : threads_) {
    if (thread.id == id) {
      return &thread;
    }
  }
  return nullptr;
}

const ChatThread *ChatStore::find(const std::string &id) const {
  for (const auto &thread : threads_) {
    if (thread.id == id) {
      return &thread;
    }
  }
  return nullptr;
}

bool ChatStore::append_message(const std::string &id, const ChatMessage &message) {
  ChatThread *thread = find(id);
  if (!thread) {
    return false;
  }
  thread->messages.push_back(message);
  return save_all();
}

bool ChatStore::update_last_usage(const std::string &id, const ChatUsage &usage) {
  ChatThread *thread = find(id);
  if (!thread) {
    return false;
  }
  thread->last_usage = usage;
  return save_all();
}

bool ChatStore::update_model(const std::string &id, const std::string &model) {
  ChatThread *thread = find(id);
  if (!thread) {
    return false;
  }
  thread->model = model;
  return save_all();
}

bool ChatStore::rename_if_default_title(const std::string &id, const std::string &prompt) {
  ChatThread *thread = find(id);
  if (!thread || thread->title != "New Chat") {
    return false;
  }
  const std::string trimmed = prompt.substr(0, std::min<size_t>(prompt.size(), 28));
  thread->title = trimmed.empty() ? "New Chat" : trimmed;
  return save_all();
}
