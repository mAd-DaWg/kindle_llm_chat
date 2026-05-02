#include "chat_store.h"
#include "json_file.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <glib.h>
#include <sstream>

extern "C" {
#include <cJSON.h>
}

namespace fs = std::filesystem;

namespace {

std::string jstr(cJSON *o, const char *key) {
  cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
  if (!cJSON_IsString(it) || !it->valuestring) {
    return {};
  }
  return std::string(it->valuestring);
}

long jint(cJSON *o, const char *key, long def = 0) {
  cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
  if (!cJSON_IsNumber(it)) {
    return def;
  }
  return static_cast<long>(it->valuedouble);
}

double jdouble(cJSON *o, const char *key, double def = 0.0) {
  cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
  if (!cJSON_IsNumber(it)) {
    return def;
  }
  return it->valuedouble;
}

} // namespace

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

  std::string raw;
  if (!read_full_file(index_path(), &raw)) {
    return false;
  }
  cJSON *root = cJSON_Parse(raw.c_str());
  if (!root || !cJSON_IsArray(root)) {
    if (root) {
      cJSON_Delete(root);
    }
    return false;
  }
  cJSON *el = nullptr;
  cJSON_ArrayForEach(el, root) {
    if (!cJSON_IsObject(el)) {
      continue;
    }
    const std::string id_s = jstr(el, "id");
    if (!id_s.empty()) {
      load_thread(id_s);
    }
  }
  cJSON_Delete(root);
  return true;
}

bool ChatStore::save_all() const {
  ensure_dirs();
  cJSON *arr = cJSON_CreateArray();
  for (const auto &thread : threads_) {
    save_thread(thread);
    cJSON *one = cJSON_CreateObject();
    cJSON_AddStringToObject(one, "id", thread.id.c_str());
    cJSON_AddItemToArray(arr, one);
  }
  char *printed = cJSON_PrintUnformatted(arr);
  cJSON_Delete(arr);
  if (!printed) {
    return false;
  }
  const std::string data(printed);
  std::free(printed);
  return write_full_file(index_path(), data);
}

bool ChatStore::save_thread(const ChatThread &thread) const {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "id", thread.id.c_str());
  cJSON_AddStringToObject(root, "title", thread.title.c_str());
  cJSON_AddStringToObject(root, "model", thread.model.c_str());
  cJSON_AddNumberToObject(root, "created_at", static_cast<double>(thread.created_at));

  cJSON *msgs = cJSON_CreateArray();
  for (const auto &message : thread.messages) {
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", message.role.c_str());
    cJSON_AddStringToObject(m, "content", message.content.c_str());
    cJSON_AddNumberToObject(m, "timestamp", static_cast<double>(message.timestamp));
    cJSON_AddItemToArray(msgs, m);
  }
  cJSON_AddItemToObject(root, "messages", msgs);

  cJSON *u = cJSON_CreateObject();
  cJSON_AddNumberToObject(u, "prompt_tokens", static_cast<double>(thread.last_usage.prompt_tokens));
  cJSON_AddNumberToObject(u, "completion_tokens", static_cast<double>(thread.last_usage.completion_tokens));
  cJSON_AddNumberToObject(u, "total_tokens", static_cast<double>(thread.last_usage.total_tokens));
  cJSON_AddNumberToObject(u, "context_percent", thread.last_usage.context_percent);
  cJSON_AddItemToObject(root, "last_usage", u);

  char *printed = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!printed) {
    return false;
  }
  const std::string data(printed);
  std::free(printed);
  const std::string path = chats_dir() + "/" + thread.id + ".json";
  return write_full_file(path, data);
}

bool ChatStore::load_thread(const std::string &id) {
  const std::string path = chats_dir() + "/" + id + ".json";
  if (!fs::exists(path)) {
    return false;
  }
  std::string raw;
  if (!read_full_file(path, &raw)) {
    return false;
  }
  cJSON *obj = cJSON_Parse(raw.c_str());
  if (!obj || !cJSON_IsObject(obj)) {
    if (obj) {
      cJSON_Delete(obj);
    }
    return false;
  }

  ChatThread thread;
  thread.id = jstr(obj, "id");
  thread.title = jstr(obj, "title");
  thread.model = jstr(obj, "model");
  thread.created_at = jint(obj, "created_at");

  cJSON *messages = cJSON_GetObjectItemCaseSensitive(obj, "messages");
  if (cJSON_IsArray(messages)) {
    cJSON *m = nullptr;
    cJSON_ArrayForEach(m, messages) {
      if (!cJSON_IsObject(m)) {
        continue;
      }
      ChatMessage msg;
      msg.role = jstr(m, "role");
      msg.content = jstr(m, "content");
      msg.timestamp = jint(m, "timestamp");
      thread.messages.push_back(msg);
    }
  }
  cJSON *last = cJSON_GetObjectItemCaseSensitive(obj, "last_usage");
  if (cJSON_IsObject(last)) {
    thread.last_usage.prompt_tokens = static_cast<int>(jint(last, "prompt_tokens"));
    thread.last_usage.completion_tokens = static_cast<int>(jint(last, "completion_tokens"));
    thread.last_usage.total_tokens = static_cast<int>(jint(last, "total_tokens"));
    thread.last_usage.context_percent = jdouble(last, "context_percent");
  }
  threads_.push_back(thread);
  cJSON_Delete(obj);
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
