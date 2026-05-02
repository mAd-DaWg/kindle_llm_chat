#include "stream_parser.h"
#include <algorithm>

extern "C" {
#include <cJSON.h>
}

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
  cJSON *obj = cJSON_Parse(json_line.c_str());
  if (!obj || !cJSON_IsObject(obj)) {
    if (obj) {
      cJSON_Delete(obj);
    }
    return;
  }

  cJSON *choices = cJSON_GetObjectItemCaseSensitive(obj, "choices");
  if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    if (cJSON_IsObject(choice)) {
      cJSON *delta = cJSON_GetObjectItemCaseSensitive(choice, "delta");
      if (cJSON_IsObject(delta)) {
        cJSON *content = cJSON_GetObjectItemCaseSensitive(delta, "content");
        if (cJSON_IsString(content) && content->valuestring && callbacks.on_token) {
          callbacks.on_token(content->valuestring);
        }
      }
    }
  }

  cJSON *usage = cJSON_GetObjectItemCaseSensitive(obj, "usage");
  if (cJSON_IsObject(usage)) {
    cJSON *pt = cJSON_GetObjectItemCaseSensitive(usage, "prompt_tokens");
    if (cJSON_IsNumber(pt)) {
      usage_.prompt_tokens = static_cast<int>(pt->valuedouble);
    }
    cJSON *ct = cJSON_GetObjectItemCaseSensitive(usage, "completion_tokens");
    if (cJSON_IsNumber(ct)) {
      usage_.completion_tokens = static_cast<int>(ct->valuedouble);
    }
    cJSON *tt = cJSON_GetObjectItemCaseSensitive(usage, "total_tokens");
    if (cJSON_IsNumber(tt)) {
      usage_.total_tokens = static_cast<int>(tt->valuedouble);
    } else {
      usage_.total_tokens = usage_.prompt_tokens + usage_.completion_tokens;
    }
    update_context_percent(usage_);
    if (callbacks.on_usage &&
        (usage_.total_tokens > 0 || usage_.prompt_tokens > 0 || usage_.completion_tokens > 0)) {
      callbacks.on_usage(usage_);
    }
  }
  cJSON_Delete(obj);
}

void StreamParser::parse_ollama_chunk(const std::string &json_line, const StreamCallbacks &callbacks) {
  cJSON *obj = cJSON_Parse(json_line.c_str());
  if (!obj || !cJSON_IsObject(obj)) {
    if (obj) {
      cJSON_Delete(obj);
    }
    return;
  }

  cJSON *message = cJSON_GetObjectItemCaseSensitive(obj, "message");
  if (cJSON_IsObject(message)) {
    cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
    if (cJSON_IsString(content) && callbacks.on_token) {
      callbacks.on_token(content->valuestring ? content->valuestring : "");
    }
  } else {
    cJSON *response = cJSON_GetObjectItemCaseSensitive(obj, "response");
    if (cJSON_IsString(response) && callbacks.on_token) {
      callbacks.on_token(response->valuestring ? response->valuestring : "");
    }
  }

  cJSON *pec = cJSON_GetObjectItemCaseSensitive(obj, "prompt_eval_count");
  if (cJSON_IsNumber(pec)) {
    usage_.prompt_tokens = static_cast<int>(pec->valuedouble);
  }
  cJSON *ec = cJSON_GetObjectItemCaseSensitive(obj, "eval_count");
  if (cJSON_IsNumber(ec)) {
    usage_.completion_tokens = static_cast<int>(ec->valuedouble);
  }
  usage_.total_tokens = usage_.prompt_tokens + usage_.completion_tokens;
  update_context_percent(usage_);
  if ((usage_.total_tokens > 0) && callbacks.on_usage) {
    callbacks.on_usage(usage_);
  }

  cJSON *done = cJSON_GetObjectItemCaseSensitive(obj, "done");
  const bool done_val =
      (cJSON_IsBool(done) && cJSON_IsTrue(done)) || (cJSON_IsNumber(done) && done->valuedouble != 0.0);
  if (done_val) {
    stream_done_ = true;
    if (callbacks.on_done) {
      callbacks.on_done();
    }
  }
  cJSON_Delete(obj);
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
