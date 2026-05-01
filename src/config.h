#pragma once

#include <string>

struct AppConfig {
  std::string base_url = "http://127.0.0.1:1234/v1";
  std::string model = "local-model";
  std::string api_key = "";
  std::string backend_mode = "openai_compatible";
  int context_window = 4096;
  int timeout_seconds = 180;
};

class ConfigStore {
public:
  explicit ConfigStore(const std::string &base_dir);
  bool load();
  bool save() const;
  const AppConfig &config() const;
  AppConfig &mutable_config();

private:
  std::string base_dir_;
  AppConfig config_;
  std::string config_path() const;
};
