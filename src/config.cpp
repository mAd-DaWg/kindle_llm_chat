#include "config.h"
#include "json_file.h"
#include <cstdlib>
#include <filesystem>

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

int jint(cJSON *o, const char *key, int def = 0) {
  cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
  if (!cJSON_IsNumber(it)) {
    return def;
  }
  return static_cast<int>(it->valuedouble);
}

} // namespace

ConfigStore::ConfigStore(const std::string &base_dir) : base_dir_(base_dir) {}

std::string ConfigStore::config_path() const { return base_dir_ + "/config.json"; }
const AppConfig &ConfigStore::config() const { return config_; }
AppConfig &ConfigStore::mutable_config() { return config_; }

bool ConfigStore::load() {
  std::error_code ec;
  fs::create_directories(base_dir_, ec);
  if (!fs::exists(config_path())) {
    return save();
  }
  std::string raw;
  if (!read_full_file(config_path(), &raw)) {
    return false;
  }
  cJSON *obj = cJSON_Parse(raw.c_str());
  if (!obj || !cJSON_IsObject(obj)) {
    if (obj) {
      cJSON_Delete(obj);
    }
    return false;
  }
  if (cJSON_GetObjectItemCaseSensitive(obj, "base_url")) {
    config_.base_url = jstr(obj, "base_url");
  }
  if (cJSON_GetObjectItemCaseSensitive(obj, "model")) {
    config_.model = jstr(obj, "model");
  }
  if (cJSON_GetObjectItemCaseSensitive(obj, "api_key")) {
    config_.api_key = jstr(obj, "api_key");
  }
  if (cJSON_GetObjectItemCaseSensitive(obj, "backend_mode")) {
    config_.backend_mode = jstr(obj, "backend_mode");
  }
  if (cJSON_GetObjectItemCaseSensitive(obj, "context_window")) {
    config_.context_window = jint(obj, "context_window");
  }
  if (cJSON_GetObjectItemCaseSensitive(obj, "timeout_seconds")) {
    config_.timeout_seconds = jint(obj, "timeout_seconds");
  }
  cJSON_Delete(obj);
  return true;
}

bool ConfigStore::save() const {
  std::error_code ec;
  fs::create_directories(base_dir_, ec);
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "base_url", config_.base_url.c_str());
  cJSON_AddStringToObject(root, "model", config_.model.c_str());
  cJSON_AddStringToObject(root, "api_key", config_.api_key.c_str());
  cJSON_AddStringToObject(root, "backend_mode", config_.backend_mode.c_str());
  cJSON_AddNumberToObject(root, "context_window", static_cast<double>(config_.context_window));
  cJSON_AddNumberToObject(root, "timeout_seconds", static_cast<double>(config_.timeout_seconds));

  char *printed = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!printed) {
    return false;
  }
  const std::string data(printed);
  std::free(printed);
  return write_full_file(config_path(), data);
}
