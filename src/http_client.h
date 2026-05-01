#pragma once

#include "config.h"
#include "stream_parser.h"
#include <atomic>
#include <glib.h>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct RequestMessage {
  std::string role;
  std::string content;
};

class HttpClient {
public:
  /** Per-request slot; threads are joined on the GLib main loop after each stream ends. */
  struct StreamSlot {
    std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
    std::thread th;
  };

  HttpClient();
  ~HttpClient();

  bool stream_chat(const AppConfig &config,
                   const std::vector<RequestMessage> &messages,
                   const StreamCallbacks &callbacks);
  bool fetch_models(const AppConfig &config,
                    std::vector<std::string> &models,
                    std::string &error);
  /** Sets cancel on every in-flight stream (Stop button, shutdown). */
  void cancel();

private:
  void join_completed_slot(std::shared_ptr<StreamSlot> slot);
  void schedule_slot_join(std::shared_ptr<StreamSlot> slot);
  static gboolean idle_join_slot(gpointer data);

  std::mutex mutex_;
  std::list<std::shared_ptr<StreamSlot>> slots_;
};
