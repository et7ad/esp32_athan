#include "athan.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/components/time/posix_tz.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>

#include "mp3_check.h"

namespace esphome {
namespace athan {

static const char *const TAG = "athan";

static const char *const LIST_KEYS[NUM_LISTS] = {"athan", "fajr", "tawashih", "tick"};
static const char *const LIST_TITLES[NUM_LISTS] = {"Athan (Dhuhr, Asr, Maghrib, Isha)", "Fajr athan",
                                                   "Pre-Fajr Tawashih", "Hourly tick"};
static const size_t CATALOG_MAX_BYTES = 64 * 1024;
static const size_t STATIONS_MAX_BYTES = 16 * 1024;
static const uint32_t HOUR_MS = 3600UL * 1000UL;

static bool due(uint64_t now, uint64_t at) { return now >= at; }

static std::string fmt_duration(uint32_t ms) {
  char b[16];
  std::snprintf(b, sizeof(b), "%u:%02u", (unsigned) (ms / 60000), (unsigned) ((ms / 1000) % 60));
  return b;
}

static std::string fmt_mb(size_t bytes) {
  char b[16];
  std::snprintf(b, sizeof(b), "%.1f MB", bytes / 1000000.0);
  return b;
}

static std::string html_escape(const std::string &s) {
  std::string o;
  o.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += c;
    }
  }
  return o;
}

// ==============================================================================================================
// /audio web page (runs in the web server's task)
// ==============================================================================================================
class AudioWebHandler : public AsyncWebHandler {
 public:
  explicit AudioWebHandler(AthanComponent *parent) : parent_(parent) {}

  bool canHandle(AsyncWebServerRequest *request) const override {
    char buf[AsyncWebServerRequest::URL_BUF_SIZE];
    StringRef url = request->url_to(buf);
    return url == "/audio" || url.starts_with("/audio/");
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    char buf[AsyncWebServerRequest::URL_BUF_SIZE];
    StringRef ref = request->url_to(buf);
    std::string url(ref.c_str(), ref.size());
    if (request->method() == HTTP_GET && (url == "/audio" || url == "/audio/")) {
      std::string page = this->parent_->render_audio_page();
      request->send(200, "text/html; charset=utf-8", page.c_str());
      return;
    }
    if (request->method() == HTTP_POST) {
      int a = std::atoi(request->arg("list").c_str());
      int b = std::atoi(request->arg("entry").c_str());
      if (url == "/audio/install")
        this->parent_->web_action(AthanComponent::WebAction::INSTALL, a, b);
      else if (url == "/audio/preview")
        this->parent_->web_action(AthanComponent::WebAction::PREVIEW, a, b);
      else if (url == "/audio/stop")
        this->parent_->web_action(AthanComponent::WebAction::STOP, 0, 0);
      // /audio/upload: the file already arrived through handleUpload()
      request->redirect("/audio");
      return;
    }
    request->send(404, "text/plain", "Not found");
  }

  void handleUpload(AsyncWebServerRequest *request, const std::string &filename, size_t index, uint8_t *data,
                    size_t len, bool final) override {
    if (final) {
      this->parent_->upload_end();
    } else if (data == nullptr && len == 0) {
      this->parent_->upload_begin(std::atoi(request->arg("slot").c_str()), filename);
    } else if (data != nullptr && len > 0) {
      this->parent_->upload_data(data, len);
    }
  }

  bool isRequestHandlerTrivial() const override { return false; }

 protected:
  AthanComponent *parent_;
};

// ==============================================================================================================
// setup / loop
// ==============================================================================================================
void AthanComponent::setup() {
  this->slots_.begin();
  this->store_.begin();
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->sound_status_ = "Ready";
    this->radio_status_ = "Radio off";
    this->prayer_status_ = "Waiting for the clock";
  }
  this->wake_ = xSemaphoreCreateCounting(64, 0);
  // 10 KB of internal RAM: an HTTPS handshake needs about 8. Low priority: audio and Wi-Fi come first.
  if (xTaskCreate(AthanComponent::worker_entry_, "athan_worker", 10240, this, 2, &this->worker_) != pdPASS) {
    ESP_LOGE(TAG, "Could not start the worker task");
    this->mark_failed();
    return;
  }
  auto *base = web_server_base::global_web_server_base;
  if (base != nullptr) {
    this->web_handler_ = new AudioWebHandler(this);  // NOLINT: lives for the life of the program
    base->add_handler(this->web_handler_);
  } else {
    ESP_LOGW(TAG, "No web server: the /audio page is not available");
  }
}

void AthanComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Athan:");
  ESP_LOGCONFIG(TAG, "  Data URL: %s", this->data_url_.c_str());
  for (int s = 0; s < NUM_SLOTS; s++)
    ESP_LOGCONFIG(TAG, "  Slot %s: %s", SLOT_NAMES[s], this->slots_.valid(s) ? this->slots_.label(s).c_str() : "(empty)");
  ESP_LOGCONFIG(TAG, "  Location: %s, stored years: %s", this->location_.c_str(),
                this->store_.years_of(this->location_).c_str());
}

void AthanComponent::loop() {
  // Actions queued by the /audio page.
  std::deque<std::pair<WebAction, std::pair<int, int>>> actions;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    actions.swap(this->web_actions_);
  }
  for (auto &a : actions) {
    switch (a.first) {
      case WebAction::INSTALL: this->install_from_catalog(a.second.first, a.second.second); break;
      case WebAction::PREVIEW: this->preview_catalog(a.second.first, a.second.second); break;
      case WebAction::STOP: this->stop_media(); break;
    }
  }

  // Track the end of a stored sound (athan, tawashih, tick) started by play_slot().
  if (this->playing_slot_ >= 0 && this->player_ != nullptr) {
    if (this->player_->state == media_player::MEDIA_PLAYER_STATE_ANNOUNCING) {
      this->playing_seen_ = true;
    } else if (this->playing_seen_ || millis() - this->playing_since_ > 5000) {
      this->playing_slot_ = -1;
    }
  }

  const uint32_t now = millis();
  if (now - this->last_tick_ < 1000)
    return;
  this->last_tick_ = now;
  const uint64_t now64 = millis_64();
  const bool online = network::is_connected();

  if (online && !this->catalog_pending_ && due(now64, this->next_catalog_try_))
    this->fetch_catalog();
  if (online && !this->stations_pending_ && due(now64, this->next_stations_try_)) {
    this->stations_pending_ = true;
    Job job;
    job.type = JobType::STATIONS;
    this->enqueue_(std::move(job));
  }
  // A slot without a sound (new device, or a power cut while writing) gets the list's first entry.
  if (online && this->catalog_ready_ && !this->sound_busy_ && this->playing_slot_ < 0 && this->slots_.ready()) {
    for (int s = 0; s < NUM_SLOTS; s++) {
      if (!this->slots_.valid(s) && this->catalog_size(s) > 0 && due(now64, this->next_default_try_[s])) {
        ESP_LOGI(TAG, "Slot %s is empty: installing the default from the list", SLOT_NAMES[s]);
        this->next_default_try_[s] = now64 + HOUR_MS / 2;  // a failed default (404, too big) is not hammered
        this->install_from_catalog(s, 0);
        break;
      }
    }
  }
  this->prayer_tick_(online);
  this->radio_tick_();
}

// ==============================================================================================================
// worker task
// ==============================================================================================================
void AthanComponent::worker_entry_(void *arg) { static_cast<AthanComponent *>(arg)->worker_loop_(); }

void AthanComponent::worker_loop_() {
  for (;;) {
    xSemaphoreTake(this->wake_, portMAX_DELAY);
    for (;;) {
      Job job;
      {
        std::lock_guard<std::mutex> lock(this->jobs_mutex_);
        if (this->jobs_.empty())
          break;
        job = std::move(this->jobs_.front());
        this->jobs_.pop_front();
      }
      this->run_job_(job);
    }
  }
}

void AthanComponent::enqueue_(Job &&job) {
  {
    std::lock_guard<std::mutex> lock(this->jobs_mutex_);
    this->jobs_.push_back(std::move(job));
  }
  xSemaphoreGive(this->wake_);
}

void AthanComponent::run_job_(Job &job) {
  switch (job.type) {
    case JobType::CATALOG: this->job_catalog_(); break;
    case JobType::STATIONS: this->job_stations_(job, false); break;
    case JobType::STATION_PLAY: this->job_stations_(job, true); break;
    case JobType::INSTALL_URL: this->job_install_url_(job); break;
    case JobType::COMMIT_BUFFER: this->job_commit_(job.slot, job.data, job.len, job.label); break;
    case JobType::PRAYER_YEAR: this->job_prayer_(job); break;
  }
}

AthanComponent::HttpResult AthanComponent::http_get_(const std::string &url, size_t max_len, uint8_t **out,
                                                     size_t *out_len, int progress_slot) {
  *out = nullptr;
  *out_len = 0;
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.timeout_ms = 20000;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 1024;
  cfg.user_agent = "esp32-athan";
  cfg.max_redirection_count = 5;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr)
    return HttpResult::FAILED;

  HttpResult result = HttpResult::FAILED;
  int64_t content_length = -1;
  int status = 0;
  for (int redirects = 0;; redirects++) {
    if (esp_http_client_open(client, 0) != ESP_OK) {
      ESP_LOGW(TAG, "Cannot reach %s", url.c_str());
      esp_http_client_cleanup(client);
      return HttpResult::FAILED;
    }
    content_length = esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && redirects < 5) {
      esp_http_client_set_redirection(client);
      esp_http_client_close(client);
      continue;
    }
    break;
  }
  if (status == 404) {
    esp_http_client_cleanup(client);
    return HttpResult::NOT_FOUND;
  }
  if (status != 200) {
    ESP_LOGW(TAG, "HTTP %d for %s", status, url.c_str());
    esp_http_client_cleanup(client);
    return HttpResult::FAILED;
  }
  if (content_length > static_cast<int64_t>(max_len)) {
    esp_http_client_cleanup(client);
    return HttpResult::TOO_BIG;
  }
  size_t cap = (content_length > 0 ? static_cast<size_t>(content_length) : max_len) + 1;
  uint8_t *buf = static_cast<uint8_t *>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) {
    esp_http_client_cleanup(client);
    return HttpResult::NO_MEMORY;
  }
  size_t len = 0;
  int last_pct = -1;
  for (;;) {
    if (len >= cap - 1) {
      // Room is exactly max_len (or the announced length): one more byte means too big.
      char probe;
      int extra = esp_http_client_read(client, &probe, 1);
      if (extra > 0) {
        result = HttpResult::TOO_BIG;
        break;
      }
      result = HttpResult::OK;
      break;
    }
    size_t want = std::min<size_t>(4096, cap - 1 - len);
    int r = esp_http_client_read(client, reinterpret_cast<char *>(buf + len), want);
    if (r < 0) {
      result = HttpResult::FAILED;
      break;
    }
    if (r == 0) {
      result = (esp_http_client_is_complete_data_received(client) || content_length < 0) && len > 0
                   ? HttpResult::OK
                   : HttpResult::FAILED;
      break;
    }
    len += r;
    if (progress_slot >= 0 && content_length > 0) {
      int pct = static_cast<int>(len * 100 / content_length);
      if (pct / 10 != last_pct / 10) {
        last_pct = pct;
        this->set_sound_status_("Downloading " + std::to_string(pct) + "%");
      }
    }
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  if (result != HttpResult::OK) {
    heap_caps_free(buf);
    return result;
  }
  buf[len] = 0;  // handy for JSON
  *out = buf;
  *out_len = len;
  return HttpResult::OK;
}

std::string AthanComponent::resolve_url_(const std::string &base, const std::string &url) const {
  if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0)
    return url;
  if (!url.empty() && url[0] == '/')
    return this->data_url_ + url;
  return base + url;
}

// ---------------- suggested lists ----------------
void AthanComponent::fetch_catalog() {
  if (this->catalog_pending_.exchange(true))
    return;
  Job job;
  job.type = JobType::CATALOG;
  this->enqueue_(std::move(job));
}

void AthanComponent::job_catalog_() {
  uint8_t *buf;
  size_t len;
  HttpResult r = this->http_get_(this->data_url_ + "/audio/catalog.json", CATALOG_MAX_BYTES, &buf, &len, -1);
  bool ok = false;
  if (r == HttpResult::OK) {
    std::vector<NamedUrl> lists[NUM_LISTS];
    JsonDocument doc = json::parse_json(buf, len);
    JsonObject root = doc.as<JsonObject>();
    const std::string base = this->data_url_ + "/audio/";
    if (!root.isNull()) {
      for (int l = 0; l < NUM_LISTS; l++) {
        for (JsonVariant e : root[LIST_KEYS[l]].as<JsonArray>()) {
          if (lists[l].size() >= MAX_ENTRIES)
            break;
          if (!e["name"].is<const char *>() || !e["url"].is<const char *>())
            continue;
          lists[l].push_back({e["name"].as<std::string>(), this->resolve_url_(base, e["url"].as<std::string>())});
        }
      }
      ok = true;
    }
    heap_caps_free(buf);
    if (ok) {
      std::lock_guard<std::mutex> lock(this->mutex_);
      for (int l = 0; l < NUM_LISTS; l++)
        this->catalog_[l] = std::move(lists[l]);
    }
  }
  this->defer([this, ok]() {
    this->catalog_pending_ = false;
    if (ok) {
      this->catalog_ready_ = true;
      this->next_catalog_try_ = millis_64() + 12 * HOUR_MS;  // pick up list changes twice a day
      ESP_LOGI(TAG, "Suggested lists loaded");
    } else {
      this->next_catalog_try_ = millis_64() + 60000;
      ESP_LOGW(TAG, "Suggested lists could not be loaded, retrying in a minute");
    }
  });
}

int AthanComponent::catalog_size(int list) const {
  if (list < 0 || list >= NUM_LISTS)
    return 0;
  std::lock_guard<std::mutex> lock(this->mutex_);
  return static_cast<int>(this->catalog_[list].size());
}

std::string AthanComponent::catalog_name(int list, int entry) const {
  if (list < 0 || list >= NUM_LISTS)
    return "";
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (entry < 0 || entry >= static_cast<int>(this->catalog_[list].size()))
    return "";
  return this->catalog_[list][entry].name;
}

void AthanComponent::preview_catalog(int list, int entry) {
  std::string url, name;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (list < 0 || list >= NUM_LISTS || entry < 0 || entry >= static_cast<int>(this->catalog_[list].size()))
      return;
    url = this->catalog_[list][entry].url;
    name = this->catalog_[list][entry].name;
  }
  this->radio_station_ = -1;  // a preview replaces the radio
  this->radio_token_++;
  this->start_stream_(url);
  this->set_sound_status_("Previewing " + name);
}

void AthanComponent::stop_media() {
  this->radio_token_++;
  this->radio_station_ = -1;
  if (this->player_ != nullptr)
    this->player_->make_call().set_command(media_player::MEDIA_PLAYER_COMMAND_STOP).set_announcement(false).perform();
  this->set_radio_status_("Radio off");
}

void AthanComponent::start_stream_(const std::string &url) {
  if (this->player_ != nullptr)
    this->player_->make_call().set_media_url(url).set_announcement(false).perform();
}

// ---------------- stored sounds ----------------
std::string AthanComponent::slot_label(int slot) const { return this->slots_.label(slot); }

bool AthanComponent::play_slot(int slot) {
  audio::AudioFile *file = this->slots_.file(slot);
  if (file == nullptr || this->player_ == nullptr)
    return false;
  this->player_->play_file(file, true, false);
  this->playing_slot_ = slot;
  this->playing_since_ = millis();
  this->playing_seen_ = false;
  return true;
}

void AthanComponent::install_from_catalog(int slot, int entry) {
  if (slot < 0 || slot >= NUM_SLOTS)
    return;
  NamedUrl e;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (entry < 0 || entry >= static_cast<int>(this->catalog_[slot].size())) {
      this->sound_status_ = "That entry is not in the list";
      return;
    }
    e = this->catalog_[slot][entry];
  }
  if (this->playing_slot_ >= 0) {
    this->set_sound_status_("Busy: a sound is playing, try again after it");
    return;
  }
  if (this->sound_busy_.exchange(true)) {
    this->set_sound_status_("Busy with another sound, try again in a moment");
    return;
  }
  this->set_sound_status_("Downloading " + e.name);
  Job job;
  job.type = JobType::INSTALL_URL;
  job.slot = slot;
  job.url = e.url;
  job.label = e.name;
  this->enqueue_(std::move(job));
}

void AthanComponent::job_install_url_(Job &job) {
  uint8_t *buf;
  size_t len;
  HttpResult r = this->http_get_(job.url, SLOT_MAX_BYTES[job.slot], &buf, &len, job.slot);
  if (r != HttpResult::OK) {
    switch (r) {
      case HttpResult::NOT_FOUND: this->set_sound_status_(job.label + ": file not found on GitHub"); break;
      case HttpResult::TOO_BIG:
        this->set_sound_status_(job.label + ": too big (limit " + fmt_mb(SLOT_MAX_BYTES[job.slot]) + "), not installed");
        break;
      case HttpResult::NO_MEMORY: this->set_sound_status_("Not enough memory to download"); break;
      default: this->set_sound_status_(job.label + ": download failed, current sound kept"); break;
    }
    this->sound_busy_ = false;
    return;
  }
  this->job_commit_(job.slot, buf, len, job.label);
}

void AthanComponent::job_commit_(int slot, uint8_t *data, size_t len, const std::string &label) {
  auto fail = [this, data](const std::string &msg) {
    heap_caps_free(data);
    this->set_sound_status_(msg);
    this->sound_busy_ = false;
  };
  if (slot < 0 || slot >= NUM_SLOTS || data == nullptr) {
    fail("Nothing to install");
    return;
  }
  if (len > SLOT_MAX_BYTES[slot]) {
    fail(label + ": too big (" + fmt_mb(len) + ", limit " + fmt_mb(SLOT_MAX_BYTES[slot]) + "), not installed");
    return;
  }
  Mp3Info info = mp3_scan(data, len);
  if (!info.ok) {
    fail(label + ": " + (info.error ? info.error : "not an MP3") + ", not installed");
    return;
  }
  if (info.duration_ms > SLOT_MAX_MS[slot]) {
    fail(label + ": too long (" + fmt_duration(info.duration_ms) + ", limit " + fmt_duration(SLOT_MAX_MS[slot]) +
         "), not installed");
    return;
  }
  // Every check passed. Ask the main loop to hide the slot (refused if that sound is playing right now).
  // The handshake state is shared, so a late answer after a timeout never touches this stack frame.
  struct Handshake {
    SemaphoreHandle_t sem{xSemaphoreCreateBinary()};
    std::atomic<bool> refused{false};
    ~Handshake() {
      if (sem != nullptr)
        vSemaphoreDelete(sem);
    }
  };
  auto hs = std::make_shared<Handshake>();
  if (hs->sem == nullptr) {
    fail("Out of memory, not installed");
    return;
  }
  this->defer([this, slot, hs]() {
    if (this->playing_slot_ == slot) {
      hs->refused = true;
    } else {
      this->slots_.invalidate(slot);
    }
    xSemaphoreGive(hs->sem);
  });
  if (xSemaphoreTake(hs->sem, pdMS_TO_TICKS(10000)) != pdTRUE) {
    fail("Device busy, not installed");
    return;
  }
  if (hs->refused) {
    fail("That sound is playing now: try again after it");
    return;
  }
  this->set_sound_status_("Saving " + label);
  bool ok = this->slots_.write(
      slot, data, len, info.duration_ms, label,
      [](void *ctx, int pct) {
        static_cast<AthanComponent *>(ctx)->set_sound_status_("Saving " + std::to_string(pct) + "%");
      },
      this);
  heap_caps_free(data);
  this->defer([this, slot, ok, label, info]() {
    this->slots_.reload(slot);
    if (ok && this->slots_.valid(slot)) {
      this->set_sound_status_("Installed " + label + " (" + fmt_duration(info.duration_ms) + ")");
    } else {
      this->set_sound_status_("Saving failed: the " + std::string(SLOT_NAMES[slot]) +
                              " slot is empty, the default will be reinstalled");
    }
    this->sound_busy_ = false;
  });
}

// ---------------- upload from the /audio page (web server task) ----------------
bool AthanComponent::upload_begin(int slot, const std::string &filename) {
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->upload_.buf != nullptr)
    heap_caps_free(this->upload_.buf);
  this->upload_ = Upload{};
  if (slot < 0 || slot >= NUM_SLOTS) {
    this->sound_status_ = "Upload: unknown slot";
    return false;
  }
  if (this->sound_busy_.exchange(true)) {
    this->sound_status_ = "Busy with another sound, upload ignored";
    return false;
  }
  size_t cap = SLOT_MAX_BYTES[slot] + 1;
  this->upload_.buf = static_cast<uint8_t *>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->upload_.buf == nullptr) {
    this->sound_status_ = "Not enough memory for the upload";
    this->sound_busy_ = false;
    return false;
  }
  this->upload_.slot = slot;
  this->upload_.cap = cap;
  std::string name = filename;
  size_t dot = name.rfind('.');
  if (dot != std::string::npos && dot > 0)
    name = name.substr(0, dot);
  if (name.size() > 60)
    name = name.substr(0, 60);
  this->upload_.name = name.empty() ? "Uploaded" : name;
  this->sound_status_ = "Receiving " + filename;
  return true;
}

void AthanComponent::upload_data(const uint8_t *data, size_t len) {
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->upload_.buf == nullptr || this->upload_.overflow)
    return;
  if (this->upload_.len + len > this->upload_.cap - 1) {
    this->upload_.overflow = true;
    return;
  }
  std::memcpy(this->upload_.buf + this->upload_.len, data, len);
  this->upload_.len += len;
}

void AthanComponent::upload_end() {
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->upload_.buf == nullptr)
    return;
  if (this->upload_.overflow) {
    heap_caps_free(this->upload_.buf);
    this->sound_status_ = this->upload_.name + ": too big (limit " + fmt_mb(SLOT_MAX_BYTES[this->upload_.slot]) +
                          "), not installed";
    this->upload_ = Upload{};
    this->sound_busy_ = false;
    return;
  }
  Job job;
  job.type = JobType::COMMIT_BUFFER;
  job.slot = this->upload_.slot;
  job.data = this->upload_.buf;
  job.len = this->upload_.len;
  job.label = this->upload_.name;
  this->upload_ = Upload{};
  this->sound_status_ = "Checking " + job.label;
  this->enqueue_(std::move(job));
}

void AthanComponent::web_action(WebAction action, int list, int entry) {
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->web_actions_.size() < 8)
    this->web_actions_.push_back({action, {list, entry}});
}

// ---------------- radio ----------------
bool AthanComponent::parse_stations_(const uint8_t *data, size_t len) {
  JsonDocument doc = json::parse_json(data, len);
  JsonObject root = doc.as<JsonObject>();
  if (root.isNull())
    return false;
  std::vector<NamedUrl> list;
  for (JsonVariant e : root["stations"].as<JsonArray>()) {
    if (list.size() >= NUM_STATIONS)
      break;
    NamedUrl s;
    if (e["name"].is<const char *>())
      s.name = e["name"].as<std::string>();
    if (e["url"].is<const char *>())
      s.url = e["url"].as<std::string>();
    list.push_back(s);
  }
  list.resize(NUM_STATIONS);
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->stations_ = std::move(list);
  return true;
}

void AthanComponent::job_stations_(Job &job, bool for_play) {
  uint8_t *buf;
  size_t len;
  bool ok = false;
  if (this->http_get_(this->data_url_ + "/radio/stations.json", STATIONS_MAX_BYTES, &buf, &len, -1) ==
      HttpResult::OK) {
    ok = this->parse_stations_(buf, len);
    heap_caps_free(buf);
  }
  std::string url, name;
  if (for_play) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (job.index >= 0 && job.index < static_cast<int>(this->stations_.size())) {
      url = this->stations_[job.index].url;  // fresh if ok, else the last copy we had
      name = this->stations_[job.index].name;
    }
  }
  uint32_t token = job.token;
  int index = job.index;
  this->defer([this, ok, for_play, url, name, token, index]() {
    if (ok) {
      this->stations_ready_ = true;
      this->next_stations_try_ = millis_64() + 6 * HOUR_MS;
    } else if (!for_play) {
      this->next_stations_try_ = millis_64() + 60000;
    }
    if (!for_play) {
      this->stations_pending_ = false;
      return;
    }
    if (token != this->radio_token_)
      return;  // stopped or changed meanwhile
    std::string label = "Radio " + std::to_string(index + 1) + (name.empty() ? "" : " " + name);
    if (url.empty()) {
      this->radio_station_ = -1;
      this->set_radio_status_(label + ": no link (station list unavailable or slot empty)");
      return;
    }
    this->radio_started_ = millis();
    this->start_stream_(url);
    this->set_radio_status_(label + (ok ? "" : " (cached link)"));
  });
}

std::string AthanComponent::own_url(int station) const {
  if (station < 0 || station >= static_cast<int>(this->radio_urls_.size()) || this->radio_urls_[station] == nullptr)
    return "";
  return this->radio_urls_[station]->state;
}

void AthanComponent::radio_play(int station, bool subscribed) {
  if (station < 0 || station >= NUM_STATIONS)
    return;
  const std::string own = this->own_url(station);
  uint8_t retries = (station == this->radio_station_) ? this->radio_retries_ : 0;
  this->radio_station_ = station;
  this->radio_subscribed_ = subscribed;
  this->radio_retries_ = retries;
  this->radio_streaming_ = false;
  this->radio_started_ = millis();
  uint32_t token = ++this->radio_token_;
  if (subscribed) {
    this->set_radio_status_("Radio " + std::to_string(station + 1) + ": getting the link");
    Job job;
    job.type = JobType::STATION_PLAY;
    job.index = station;
    job.token = token;
    this->enqueue_(std::move(job));
  } else if (own.empty()) {
    this->radio_station_ = -1;
    this->set_radio_status_("Radio " + std::to_string(station + 1) + " is empty");
  } else {
    this->start_stream_(own);
    this->set_radio_status_("Radio " + std::to_string(station + 1) + " (own link)");
  }
}

void AthanComponent::radio_stop() { this->stop_media(); }

bool AthanComponent::station_available(int station, bool subscribed) const {
  if (station < 0 || station >= NUM_STATIONS)
    return false;
  if (!subscribed)
    return !this->own_url(station).empty();
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->stations_.empty())
    return true;  // list not loaded yet: try it
  return !this->stations_[station].url.empty();
}

std::string AthanComponent::station_name(int station) const {
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (station < 0 || station >= static_cast<int>(this->stations_.size()))
    return "";
  return this->stations_[station].name;
}

void AthanComponent::radio_tick_() {
  if (this->radio_station_ < 0 || this->player_ == nullptr)
    return;
  auto st = this->player_->state;
  if (st == media_player::MEDIA_PLAYER_STATE_PLAYING || st == media_player::MEDIA_PLAYER_STATE_ANNOUNCING ||
      st == media_player::MEDIA_PLAYER_STATE_PAUSED) {
    this->radio_streaming_ = true;
    this->radio_retries_ = 0;
    return;
  }
  // Not playing: still starting (link fetch + connect), or the stream dropped.
  if (millis() - this->radio_started_ < 15000)
    return;
  if (this->radio_retries_ < 3) {
    this->radio_retries_++;
    ESP_LOGW(TAG, "Radio stream stopped, reconnecting (%u/3)", this->radio_retries_);
    this->radio_play(this->radio_station_, this->radio_subscribed_);
    this->set_radio_status_("Radio " + std::to_string(this->radio_station_ + 1) + ": reconnecting (" +
                            std::to_string(this->radio_retries_) + "/3)");
    return;
  }
  int s = this->radio_station_;
  this->radio_station_ = -1;
  this->radio_retries_ = 0;
  this->set_radio_status_("Radio " + std::to_string(s + 1) + " stopped: station not reachable");
}

// ==============================================================================================================
// prayer times
// ==============================================================================================================
void AthanComponent::set_location(const std::string &key) {
  if (key == this->location_)
    return;
  this->location_ = key;
  this->loaded_key_.clear();
  this->cur_not_published_ = false;
  this->next_cur_try_ = this->next_prev_try_ = this->next_next_try_ = millis_64();
  this->schedule_version_++;
  ESP_LOGI(TAG, "Location: %s", key.c_str());
}

void AthanComponent::refresh_prayer_times() {
  this->force_refresh_ = true;
  this->next_cur_try_ = millis_64();
}

void AthanComponent::apply_tz_(const std::string &tz) {
  if (tz.empty() || (this->tz_valid_ && tz == this->tz_text_))
    return;
  TzInfo t;
  if (!parse_posix_tz(tz.c_str(), &t)) {
    ESP_LOGW(TAG, "Bad time zone '%s'", tz.c_str());
    return;
  }
#ifndef USE_TIME_TIMEZONE
#error "athan needs a time zone on the sntp time component (any POSIX string; it is replaced at runtime)"
#endif
  time::ParsedTimezone p{};
  p.std_offset_seconds = t.std_offset;
  p.dst_offset_seconds = t.dst_offset;
  auto conv = [](const TzRule &r, time::DSTRule *o) {
    o->time_seconds = r.time_seconds;
    o->day = r.day;
    o->month = r.month;
    o->week = r.week;
    o->day_of_week = r.day_of_week;
    switch (r.type) {
      case TzRuleType::MONTH_WEEK_DAY: o->type = time::DSTRuleType::MONTH_WEEK_DAY; break;
      case TzRuleType::JULIAN_NO_LEAP: o->type = time::DSTRuleType::JULIAN_NO_LEAP; break;
      case TzRuleType::DAY_OF_YEAR: o->type = time::DSTRuleType::DAY_OF_YEAR; break;
      default: o->type = time::DSTRuleType::NONE; break;
    }
  };
  if (t.has_dst) {
    conv(t.start, &p.dst_start);
    conv(t.end, &p.dst_end);
  } else {
    p.dst_start.type = time::DSTRuleType::NONE;
    p.dst_end.type = time::DSTRuleType::NONE;
  }
  time::set_global_tz(p);
  this->tz_ = t;
  this->tz_text_ = tz;
  this->tz_valid_ = true;
  ESP_LOGI(TAG, "Time zone: %s", tz.c_str());
}

void AthanComponent::load_tables_(int year) {
  this->cur_table_.clear();
  this->prev_table_.clear();
  std::string tz;
  int s = this->store_.find(this->location_, year);
  if (s >= 0)
    this->store_.load(s, &this->cur_table_, &tz);
  if (this->cur_table_.empty()) {
    int p = this->store_.find(this->location_, year - 1);
    if (p >= 0)
      this->store_.load(p, &this->prev_table_, &tz);
  }
  if (tz.empty())
    tz = this->store_.any_tz(this->location_);
  this->apply_tz_(tz);
  this->standin_ = this->cur_table_.empty() && !this->prev_table_.empty();
  this->loaded_key_ = this->location_;
  this->loaded_year_ = year;
  this->schedule_version_++;
}

bool AthanComponent::today_times(uint8_t hours[7], uint8_t minutes[7]) {
  if (this->time_ == nullptr || this->location_.empty())
    return false;
  ESPTime now = this->time_->now();
  if (!now.is_valid())
    return false;
  if (this->loaded_key_ != this->location_ || this->loaded_year_ != now.year)
    this->load_tables_(now.year);
  if (!this->cur_table_.empty()) {
    size_t base = static_cast<size_t>(now.day_of_year - 1) * NUM_FIELDS;
    if (base + NUM_FIELDS > this->cur_table_.size())
      return false;
    for (int i = 0; i < 7; i++) {
      uint16_t v = this->cur_table_[base + ADHAN_COLUMN[i]];
      hours[i] = v / 60;
      minutes[i] = v % 60;
    }
    return true;
  }
  if (!this->prev_table_.empty() && this->tz_valid_) {
    // Stand-in: same calendar date last year, carried over through UTC so DST-change dates stay right.
    const int month = now.month, day = now.day_of_month;
    const int pday = (month == 2 && day == 29) ? 28 : day;
    size_t base = static_cast<size_t>(day_of_year(now.year - 1, month, pday) - 1) * NUM_FIELDS;
    if (base + NUM_FIELDS > this->prev_table_.size())
      return false;
    for (int i = 0; i < 7; i++) {
      int v = this->prev_table_[base + ADHAN_COLUMN[i]];
      int32_t off_prev = tz_offset_at_utc(this->tz_, tz_local_to_utc(this->tz_, now.year - 1, month, pday, v));
      int32_t off_cur = tz_offset_at_utc(this->tz_, tz_local_to_utc(this->tz_, now.year, month, day, v));
      int adj = v + (off_prev - off_cur) / 60;
      adj = std::max(0, std::min(1439, adj));
      hours[i] = adj / 60;
      minutes[i] = adj % 60;
    }
    return true;
  }
  return false;
}

void AthanComponent::prayer_tick_(bool online) {
  if (this->location_.empty() || this->time_ == nullptr || !this->store_.ready())
    return;
  ESPTime now = this->time_->now();
  if (!now.is_valid())
    return;
  const int year = now.year;
  if (this->loaded_key_ != this->location_ || this->loaded_year_ != year)
    this->load_tables_(year);
  if (now.day_of_year != this->last_doy_) {
    this->last_doy_ = now.day_of_year;
    this->schedule_version_++;
  }

  // Status line for the web page and the OLED Info screen.
  std::string years = this->store_.years_of(this->location_);
  std::string status;
  if (!this->cur_table_.empty())
    status = this->location_ + " " + std::to_string(year) + " (stored: " + years + ")";
  else if (this->standin_)
    status = this->location_ + ": using " + std::to_string(year - 1) + " times until the " + std::to_string(year) +
             " timetable is published";
  else if (this->prayer_job_pending_)
    status = this->location_ + ": downloading " + std::to_string(year);
  else
    status = this->location_ + ": no prayer times yet" + (online ? "" : " (no internet)");
  this->set_prayer_status_(status);

  if (!online || this->prayer_job_pending_)
    return;
  const uint64_t ms = millis_64();
  Job job;
  job.type = JobType::PRAYER_YEAR;
  job.key = this->location_;
  if ((this->cur_table_.empty() || this->force_refresh_) && due(ms, this->next_cur_try_)) {
    job.year = year;
    job.purpose = PrayerPurpose::CURRENT;
    this->force_refresh_ = false;
  } else if (this->cur_table_.empty() && this->cur_not_published_ && this->prev_table_.empty() &&
             due(ms, this->next_prev_try_)) {
    job.year = year - 1;  // nothing at all stored (new device in January): fetch last year for the stand-in
    job.purpose = PrayerPurpose::PREVIOUS;
  } else if (now.month == 12 && !this->store_.has(this->location_, year + 1) && due(ms, this->next_next_try_)) {
    job.year = year + 1;
    job.purpose = PrayerPurpose::NEXT;
  } else {
    return;
  }
  this->prayer_job_pending_ = true;
  this->enqueue_(std::move(job));
}

void AthanComponent::job_prayer_(Job &job) {
  const std::string url = this->data_url_ + "/athantimes/" + job.key + "/" + std::to_string(job.year) + ".json";
  uint8_t *buf;
  size_t len;
  HttpResult r = this->http_get_(url, MAX_YEAR_FILE_BYTES, &buf, &len, -1);
  enum Outcome { O_STORED, O_NOT_PUBLISHED, O_NETWORK, O_INVALID } outcome = O_NETWORK;
  std::string err;
  if (r == HttpResult::NOT_FOUND) {
    outcome = O_NOT_PUBLISHED;
  } else if (r == HttpResult::OK) {
    std::vector<uint16_t> table;
    std::string tz;
    if (parse_year_file(buf, len, job.key, job.year, &table, &tz, &err)) {
      std::vector<std::pair<std::string, int>> keep = {
          {job.key, job.year - 1}, {job.key, job.year}, {job.key, job.year + 1}};
      outcome = this->store_.write(job.key, job.year, tz, table, keep) ? O_STORED : O_INVALID;
      if (outcome == O_INVALID)
        err = "flash write failed";
    } else {
      outcome = O_INVALID;
    }
    heap_caps_free(buf);
  } else if (r == HttpResult::TOO_BIG) {
    outcome = O_INVALID;
    err = "file over 100 KB";
  }
  if (outcome == O_INVALID)
    ESP_LOGW(TAG, "%s %d refused: %s", job.key.c_str(), job.year, err.c_str());
  const std::string key = job.key;
  const int year = job.year;
  const PrayerPurpose purpose = job.purpose;
  this->defer([this, key, year, purpose, outcome]() {
    this->prayer_job_pending_ = false;
    const uint64_t ms = millis_64();
    if (outcome == O_STORED) {
      ESP_LOGI(TAG, "Prayer times %s %d stored", key.c_str(), year);
      if (purpose == PrayerPurpose::CURRENT)
        this->cur_not_published_ = false;
      if (key == this->location_)
        this->loaded_key_.clear();  // reload tables (and the time zone) on the next tick
      return;
    }
    switch (purpose) {
      case PrayerPurpose::CURRENT:
        if (outcome == O_NOT_PUBLISHED) {
          // Normal while a mosque has not published yet (sometimes days into January).
          this->cur_not_published_ = true;
          this->next_cur_try_ = ms + (this->prev_table_.empty() ? HOUR_MS / 2 : 6 * HOUR_MS);
        } else {
          this->next_cur_try_ = ms + (outcome == O_INVALID ? HOUR_MS / 2 : 120000);
        }
        break;
      case PrayerPurpose::PREVIOUS:
        this->next_prev_try_ = ms + (outcome == O_NETWORK ? 300000 : 6 * HOUR_MS);
        break;
      case PrayerPurpose::NEXT:
        // Not found in December is expected; check again tomorrow.
        this->next_next_try_ = ms + (outcome == O_NETWORK ? HOUR_MS : 24 * HOUR_MS);
        break;
    }
  });
}

// ==============================================================================================================
// status strings
// ==============================================================================================================
void AthanComponent::set_sound_status_(const std::string &s) {
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->sound_status_ = s;
}
void AthanComponent::set_radio_status_(const std::string &s) {
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->radio_status_ = s;
}
void AthanComponent::set_prayer_status_(const std::string &s) {
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->prayer_status_ = s;
}
std::string AthanComponent::sound_status() const {
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->sound_status_;
}
std::string AthanComponent::radio_status() const {
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->radio_status_;
}
std::string AthanComponent::prayer_status() const {
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->prayer_status_;
}

// ==============================================================================================================
// /audio page
// ==============================================================================================================
std::string AthanComponent::render_audio_page() {
  std::vector<NamedUrl> lists[NUM_LISTS];
  std::string status;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    for (int l = 0; l < NUM_LISTS; l++)
      lists[l] = this->catalog_[l];
    status = this->sound_status_;
  }
  const bool busy = this->sound_busy_.load();
  std::string h;
  h.reserve(12000);
  h += "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
       "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  if (busy)
    h += "<meta http-equiv=\"refresh\" content=\"3\">";
  h += "<title>Athan sounds</title><style>"
       "body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;max-width:760px;margin:0 auto;padding:16px;"
       "color:#1f2937;background:#fafafa}h1{font-size:1.4em}h2{font-size:1.1em;margin-top:1.6em;"
       "border-top:1px solid #ddd;padding-top:.8em}.st{background:#eef6f3;padding:8px 12px;border-radius:6px}"
       "table{border-collapse:collapse;width:100%}td{padding:4px 6px;border-bottom:1px solid #eee}"
       "form{display:inline;margin:0}button{padding:4px 10px}.hint{color:#6b7280;font-size:.9em}"
       "</style></head><body>";
  h += "<p><a href=\"/\">&larr; Device page</a></p><h1>Athan sounds</h1>";
  h += "<p class=\"st\">" + html_escape(status) + (busy ? " &hellip;" : "") + "</p>";
  h += "<form method=\"post\" action=\"/audio/stop\"><button>Stop preview</button></form>";
  h += "<p class=\"hint\">Preview streams the sound from the internet; nothing is stored. Install downloads it, "
       "checks it, and only then replaces the sound on the device. A failed download or a file over the limit "
       "never touches the installed sound.</p>";
  for (int s = 0; s < NUM_SLOTS; s++) {
    h += "<h2>" + std::string(LIST_TITLES[s]) + "</h2><p>Installed: ";
    if (this->slots_.valid(s))
      h += "<b>" + html_escape(this->slots_.label(s)) + "</b> (" + fmt_duration(this->slots_.duration_ms(s)) + ")";
    else
      h += "<i>none</i>";
    h += "</p>";
    if (lists[s].empty()) {
      h += "<p class=\"hint\">Suggested list not loaded yet (no internet?).</p>";
    } else {
      h += "<table>";
      for (size_t e = 0; e < lists[s].size(); e++) {
        std::string q = "?list=" + std::to_string(s) + "&amp;entry=" + std::to_string(e);
        h += "<tr><td>" + html_escape(lists[s][e].name) + "</td><td>";
        h += "<form method=\"post\" action=\"/audio/preview" + q + "\"><button>Preview</button></form> ";
        h += "<form method=\"post\" action=\"/audio/install" + q + "\"><button>Install</button></form>";
        h += "</td></tr>";
      }
      h += "</table>";
    }
    h += "<p>Upload your own: <form method=\"post\" action=\"/audio/upload?slot=" + std::to_string(s) +
         "\" enctype=\"multipart/form-data\"><input type=\"file\" name=\"file\" accept=\".mp3,audio/mpeg\" required> "
         "<button>Upload</button></form></p>";
    h += "<p class=\"hint\">MP3, at most " + fmt_mb(SLOT_MAX_BYTES[s]) + " and " + fmt_duration(SLOT_MAX_MS[s]) +
         " min.</p>";
  }
  h += "</body></html>";
  return h;
}

}  // namespace athan
}  // namespace esphome
