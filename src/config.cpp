#include "config.h"
#include <filesystem>
#include <json-glib/json-glib.h>

namespace fs = std::filesystem;

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
  JsonParser *parser = json_parser_new();
  GError *error = nullptr;
  if (!json_parser_load_from_file(parser, config_path().c_str(), &error)) {
    if (error) {
      g_error_free(error);
    }
    g_object_unref(parser);
    return false;
  }

  JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
  if (json_object_has_member(obj, "base_url")) {
    config_.base_url = json_object_get_string_member(obj, "base_url");
  }
  if (json_object_has_member(obj, "model")) {
    config_.model = json_object_get_string_member(obj, "model");
  }
  if (json_object_has_member(obj, "api_key")) {
    config_.api_key = json_object_get_string_member(obj, "api_key");
  }
  if (json_object_has_member(obj, "backend_mode")) {
    config_.backend_mode = json_object_get_string_member(obj, "backend_mode");
  }
  if (json_object_has_member(obj, "context_window")) {
    config_.context_window = json_object_get_int_member(obj, "context_window");
  }
  if (json_object_has_member(obj, "timeout_seconds")) {
    config_.timeout_seconds = json_object_get_int_member(obj, "timeout_seconds");
  }
  g_object_unref(parser);
  return true;
}

bool ConfigStore::save() const {
  std::error_code ec;
  fs::create_directories(base_dir_, ec);
  JsonBuilder *builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "base_url");
  json_builder_add_string_value(builder, config_.base_url.c_str());
  json_builder_set_member_name(builder, "model");
  json_builder_add_string_value(builder, config_.model.c_str());
  json_builder_set_member_name(builder, "api_key");
  json_builder_add_string_value(builder, config_.api_key.c_str());
  json_builder_set_member_name(builder, "backend_mode");
  json_builder_add_string_value(builder, config_.backend_mode.c_str());
  json_builder_set_member_name(builder, "context_window");
  json_builder_add_int_value(builder, config_.context_window);
  json_builder_set_member_name(builder, "timeout_seconds");
  json_builder_add_int_value(builder, config_.timeout_seconds);
  json_builder_end_object(builder);

  JsonGenerator *gen = json_generator_new();
  JsonNode *node = json_builder_get_root(builder);
  json_generator_set_root(gen, node);
  GError *error = nullptr;
  json_generator_to_file(gen, config_path().c_str(), &error);
  if (error) {
    g_error_free(error);
  }
  json_node_free(node);
  g_object_unref(gen);
  g_object_unref(builder);
  return error == nullptr;
}
