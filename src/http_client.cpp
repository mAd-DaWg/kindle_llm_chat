#include "http_client.h"
#include <curl/curl.h>
#include <glib.h>
#include <cstdlib>
#include <future>
#include <memory>
#include <thread>

extern "C" {
#include <cJSON.h>
}

namespace {
struct CurlCtx {
  StreamParser *parser = nullptr;
  StreamCallbacks callbacks;
  std::atomic<bool> *cancel_requested = nullptr;
};

size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  CurlCtx *ctx = static_cast<CurlCtx *>(userdata);
  const size_t bytes = size * nmemb;
  if (ctx->cancel_requested && ctx->cancel_requested->load()) {
    return 0;
  }
  ctx->parser->feed(ptr, bytes, ctx->callbacks);
  return bytes;
}

int progress_cb(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  CurlCtx *ctx = static_cast<CurlCtx *>(clientp);
  return (ctx->cancel_requested && ctx->cancel_requested->load()) ? 1 : 0;
}

size_t fetch_models_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *out = static_cast<std::string *>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

std::string trim_trailing_slash(std::string s) {
  while (!s.empty() && s.back() == '/') {
    s.pop_back();
  }
  return s;
}

std::string api_root_from_base_url(const std::string &base_url) {
  std::string u = trim_trailing_slash(base_url);
  const std::string scheme_sep = "://";
  const std::size_t scheme_pos = u.find(scheme_sep);
  if (scheme_pos == std::string::npos) {
    return u;
  }
  const std::size_t host_start = scheme_pos + scheme_sep.size();
  const std::size_t path_start = u.find('/', host_start);
  if (path_start == std::string::npos) {
    return u;
  }
  return u.substr(0, path_start);
}

struct FetchModelsOutcome {
  bool ok = false;
  std::vector<std::string> models;
  std::string error;
};

std::string models_list_url(const AppConfig &cfg) {
  if (cfg.backend_mode == "ollama") {
    return trim_trailing_slash(api_root_from_base_url(cfg.base_url)) + "/api/tags";
  }
  /* OpenAI-compatible (LM Studio, etc.): base_url is e.g. http://127.0.0.1:1234/v1 */
  return trim_trailing_slash(cfg.base_url) + "/models";
}

bool parse_models_json(const AppConfig &cfg, cJSON *root, std::vector<std::string> &models, std::string &error) {
  models.clear();
  if (!root || !cJSON_IsObject(root)) {
    error = "Empty model list JSON";
    return false;
  }
  if (cfg.backend_mode == "ollama") {
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "models");
    if (!cJSON_IsArray(arr)) {
      error = "Ollama model list missing \"models\" array";
      return false;
    }
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, arr) {
      if (!cJSON_IsObject(item)) {
        continue;
      }
      cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
      if (cJSON_IsString(name) && name->valuestring && name->valuestring[0]) {
        models.emplace_back(name->valuestring);
      }
    }
  } else {
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
      error = "Model list JSON missing \"data\" array (expected OpenAI-compatible /v1/models)";
      return false;
    }
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, data) {
      if (!cJSON_IsObject(item)) {
        continue;
      }
      cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
      if (cJSON_IsString(id) && id->valuestring && id->valuestring[0]) {
        models.emplace_back(id->valuestring);
      }
    }
  }
  if (models.empty()) {
    error = "Model list contained no model names";
    return false;
  }
  return true;
}

FetchModelsOutcome fetch_models_worker(AppConfig cfg) {
  FetchModelsOutcome out;
  CURL *curl = curl_easy_init();
  if (!curl) {
    out.error = "Could not initialize libcurl";
    return out;
  }

  std::string response;
  const std::string url = models_list_url(cfg);
  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Accept: application/json");
  if (!cfg.api_key.empty()) {
    std::string auth = "Authorization: Bearer " + cfg.api_key;
    headers = curl_slist_append(headers, auth.c_str());
  }

  const long timeout_sec = cfg.timeout_seconds > 0 ? cfg.timeout_seconds : 60L;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetch_models_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

  CURLcode rc = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (rc != CURLE_OK) {
    out.error = curl_easy_strerror(rc);
    return out;
  }
  if (http_code < 200 || http_code >= 300) {
    out.error = "Model list request failed with HTTP " + std::to_string(http_code);
    return out;
  }

  cJSON *root = cJSON_Parse(response.c_str());
  if (!root) {
    out.error = "Failed to parse model list JSON";
    return out;
  }
  std::string parse_err;
  if (!parse_models_json(cfg, root, out.models, parse_err)) {
    cJSON_Delete(root);
    out.error = parse_err;
    return out;
  }
  cJSON_Delete(root);

  out.ok = true;
  return out;
}
} // namespace

HttpClient::HttpClient() {}

HttpClient::~HttpClient() {
  cancel();
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &s : slots_) {
    if (s->th.joinable()) {
      s->th.join();
    }
  }
  slots_.clear();
}

void HttpClient::cancel() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &s : slots_) {
    s->cancel->store(true);
  }
}

void HttpClient::join_completed_slot(std::shared_ptr<StreamSlot> slot) {
  if (slot->th.joinable()) {
    slot->th.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  slots_.remove_if([&](const std::shared_ptr<StreamSlot> &s) { return s.get() == slot.get(); });
}

void HttpClient::schedule_slot_join(std::shared_ptr<StreamSlot> slot) {
  using Pack = std::pair<HttpClient *, std::shared_ptr<StreamSlot>>;
  auto *pack = new Pack(this, std::move(slot));
  g_main_context_invoke(nullptr, &HttpClient::idle_join_slot, pack);
}

gboolean HttpClient::idle_join_slot(gpointer data) {
  auto *pack = static_cast<std::pair<HttpClient *, std::shared_ptr<StreamSlot>> *>(data);
  pack->first->join_completed_slot(std::move(pack->second));
  delete pack;
  return FALSE;
}

bool HttpClient::stream_chat(const AppConfig &config,
                             const std::vector<RequestMessage> &messages,
                             const StreamCallbacks &callbacks) {
  auto slot = std::make_shared<StreamSlot>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    slots_.push_back(slot);
  }

  slot->th = std::thread([this, slot, config, messages, callbacks]() {
    CURL *curl = curl_easy_init();
    if (!curl) {
      if (callbacks.on_error) {
        callbacks.on_error("Could not initialize libcurl");
      }
      schedule_slot_join(slot);
      return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", config.model.c_str());
    cJSON_AddTrueToObject(root, "stream");
    if (config.backend_mode != "ollama") {
      cJSON *so = cJSON_CreateObject();
      cJSON_AddTrueToObject(so, "include_usage");
      cJSON_AddItemToObject(root, "stream_options", so);
    }
    cJSON *arr = cJSON_CreateArray();
    for (const auto &msg : messages) {
      cJSON *m = cJSON_CreateObject();
      cJSON_AddStringToObject(m, "role", msg.role.c_str());
      cJSON_AddStringToObject(m, "content", msg.content.c_str());
      cJSON_AddItemToArray(arr, m);
    }
    cJSON_AddItemToObject(root, "messages", arr);

    char *json_payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_payload) {
      if (callbacks.on_error) {
        callbacks.on_error("Could not build request JSON");
      }
      curl_easy_cleanup(curl);
      schedule_slot_join(slot);
      return;
    }
    std::string post_body(json_payload);
    std::free(json_payload);

    std::string url = config.base_url;
    if (config.backend_mode == "ollama") {
      if (!url.empty() && url.back() == '/') {
        url.pop_back();
      }
      url += "/api/chat";
    } else {
      if (!url.empty() && url.back() == '/') {
        url.pop_back();
      }
      url += "/chat/completions";
    }

    StreamParser parser(config);
    CurlCtx ctx{&parser, callbacks, slot->cancel.get()};

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!config.api_key.empty()) {
      std::string auth = "Authorization: Bearer " + config.api_key;
      headers = curl_slist_append(headers, auth.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post_body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config.timeout_seconds);

    CURLcode rc = curl_easy_perform(curl);
    if (slot->cancel->load()) {
      parser.finish(callbacks);
    } else if (rc != CURLE_OK) {
      if (callbacks.on_error) {
        callbacks.on_error(curl_easy_strerror(rc));
      }
    } else {
      parser.finish(callbacks);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    schedule_slot_join(slot);
  });
  return true;
}

bool HttpClient::fetch_models(const AppConfig &config,
                              std::vector<std::string> &models,
                              std::string &error) {
  models.clear();
  error.clear();

  /* Run libcurl off the GTK main thread; return results by value (no cross-thread
   * writes to caller stack). */
  AppConfig cfg_copy = config;
  std::future<FetchModelsOutcome> fut =
      std::async(std::launch::async, fetch_models_worker, std::move(cfg_copy));
  FetchModelsOutcome out = fut.get();
  if (!out.ok) {
    error = out.error;
    return false;
  }
  models = std::move(out.models);
  return true;
}
