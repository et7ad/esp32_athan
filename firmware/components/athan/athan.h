#pragma once
// athan: the version 3 parts ESPHome does not provide.
//
//  * Four replaceable sounds in flash (athan, fajr, tawashih, tick): install from the suggested list on GitHub or
//    upload from the /audio web page; every file is checked (MP3, size, duration) in PSRAM before flash is touched.
//  * The suggested lists (docs/audio/catalog.json) and the radio stations (docs/radio/stations.json).
//  * Radio: ten slots, each either subscribed to the project's station list (link fetched fresh on every play)
//    or playing the owner's own URL.
//  * Prayer times: one yearly file per mosque (docs/athantimes/<key>/<year>.json), stored in flash, next year
//    fetched from 1 December, last year's times as a stand-in until the new year is published, runtime time zone.
//
// Everything slow (HTTPS, flash erase/write, JSON parsing) runs on one worker task. Results come back to the main
// loop through Component::defer(), which is safe to call from other tasks.
//
// The yaml (firmware/athan.yaml) calls the public methods below from lambdas; see DEVELOPER.md for the full API.

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "esphome/components/speaker/media_player/speaker_media_player.h"
#include "esphome/components/text/text.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "audio_slots.h"
#include "prayer_store.h"
#include "tz_posix.h"

namespace esphome {
namespace athan {

static const int NUM_LISTS = 4;  // athan, fajr, tawashih, tick: list i fills slot i
static const int NUM_STATIONS = 10;
static const int MAX_ENTRIES = 10;

struct NamedUrl {
  std::string name;
  std::string url;
};

class AudioWebHandler;

class AthanComponent : public Component {
 public:
  void set_media_player(speaker::SpeakerMediaPlayer *player) { this->player_ = player; }
  void set_time(time::RealTimeClock *time) { this->time_ = time; }
  void set_data_url(const std::string &url) { this->data_url_ = url; }
  void add_radio_url(text::Text *t) { this->radio_urls_.push_back(t); }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  // ---------------- sounds stored on the device (slot 0 athan, 1 fajr, 2 tawashih, 3 tick) ----------------
  bool slot_valid(int slot) const { return this->slots_.valid(slot); }
  std::string slot_label(int slot) const;
  /// Play a stored sound on the announcement pipeline. False if the slot is empty or being rewritten.
  bool play_slot(int slot);
  /// True while a stored sound started by play_slot() is playing.
  bool slot_playing() const { return this->playing_slot_ >= 0; }
  /// Download entry `entry` of the suggested list for `slot` and install it (checks first).
  void install_from_catalog(int slot, int entry);
  bool sound_busy() const { return this->sound_busy_.load(); }
  std::string sound_status() const;

  // ---------------- suggested lists ----------------
  void fetch_catalog();
  bool catalog_ready() const { return this->catalog_ready_.load(); }
  int catalog_size(int list) const;
  std::string catalog_name(int list, int entry) const;
  /// Stream an entry for listening only (media pipeline, nothing stored).
  void preview_catalog(int list, int entry);
  /// Stop whatever plays on the media pipeline (preview or radio).
  void stop_media();

  // ---------------- radio ----------------
  /// Play radio slot `station` (0..9). Subscribed: the link is fetched from the project's station list first;
  /// otherwise the slot's own link (the radio_urls text entities) is used.
  void radio_play(int station, bool subscribed);
  void radio_stop();
  /// Station index playing (or starting), -1 if none.
  int radio_active() const { return this->radio_station_; }
  bool radio_subscribed_active() const { return this->radio_subscribed_; }
  /// Whether a slot has something to play (subscribed: the list has a link there; own: link not empty).
  bool station_available(int station, bool subscribed) const;
  /// The slot's own link ("" if none).
  std::string own_url(int station) const;
  /// Name from the project's station list ("" if unknown).
  std::string station_name(int station) const;
  std::string radio_status() const;

  // ---------------- prayer times ----------------
  void set_location(const std::string &key);
  const std::string &location() const { return this->location_; }
  /// Changes whenever today's times may have changed (new day, new data, new location).
  uint32_t schedule_version() const { return this->schedule_version_; }
  /// Today's Fajr, Sunrise, Doha, Dhuhr, Asr, Maghrib, Isha (local time). False if none known.
  bool today_times(uint8_t hours[7], uint8_t minutes[7]);
  /// Today's times come from last year (this year's file not published yet).
  bool times_standin() const { return this->standin_; }
  /// Download the current year again (the "Refresh Prayer Times" button).
  void refresh_prayer_times();
  std::string prayer_status() const;

  // ---------------- used by the /audio web page (httpd task) ----------------
  enum class WebAction : uint8_t { INSTALL, PREVIEW, STOP };
  void web_action(WebAction action, int list, int entry);
  bool upload_begin(int slot, const std::string &filename);
  void upload_data(const uint8_t *data, size_t len);
  void upload_end();
  std::string render_audio_page();

 protected:
  enum class JobType : uint8_t { CATALOG, STATIONS, STATION_PLAY, INSTALL_URL, COMMIT_BUFFER, PRAYER_YEAR };
  enum class PrayerPurpose : uint8_t { CURRENT, PREVIOUS, NEXT };
  enum class HttpResult : uint8_t { OK, NOT_FOUND, TOO_BIG, FAILED, NO_MEMORY };
  struct Job {
    JobType type;
    int slot{-1};
    int index{-1};
    int year{0};
    uint32_t token{0};
    PrayerPurpose purpose{PrayerPurpose::CURRENT};
    std::string url;
    std::string label;
    std::string key;
    uint8_t *data{nullptr};
    size_t len{0};
  };

  // worker
  static void worker_entry_(void *arg);
  void worker_loop_();
  void enqueue_(Job &&job);
  void run_job_(Job &job);
  HttpResult http_get_(const std::string &url, size_t max_len, uint8_t **out, size_t *out_len, int progress_slot);
  void job_catalog_();
  void job_stations_(Job &job, bool for_play);
  void job_install_url_(Job &job);
  void job_commit_(int slot, uint8_t *data, size_t len, const std::string &label);
  void job_prayer_(Job &job);
  bool parse_stations_(const uint8_t *data, size_t len);

  // main loop helpers
  void prayer_tick_(bool online);
  void load_tables_(int year);
  void apply_tz_(const std::string &tz);
  void radio_tick_();
  void start_stream_(const std::string &url);
  void set_sound_status_(const std::string &s);
  void set_radio_status_(const std::string &s);
  void set_prayer_status_(const std::string &s);
  std::string resolve_url_(const std::string &base, const std::string &url) const;

  speaker::SpeakerMediaPlayer *player_{nullptr};
  time::RealTimeClock *time_{nullptr};
  std::string data_url_;
  std::vector<text::Text *> radio_urls_;

  AudioSlots slots_;
  PrayerStore store_;
  AudioWebHandler *web_handler_{nullptr};

  // worker task
  TaskHandle_t worker_{nullptr};
  SemaphoreHandle_t wake_{nullptr};
  std::mutex jobs_mutex_;
  std::deque<Job> jobs_;

  // state shared with the worker and the web page (guarded by mutex_)
  mutable std::mutex mutex_;
  std::vector<NamedUrl> catalog_[NUM_LISTS];
  std::vector<NamedUrl> stations_;
  std::string sound_status_;
  std::string radio_status_;
  std::string prayer_status_;
  std::deque<std::pair<WebAction, std::pair<int, int>>> web_actions_;
  struct Upload {
    int slot{-1};
    uint8_t *buf{nullptr};
    size_t len{0};
    size_t cap{0};
    bool overflow{false};
    std::string name;
  } upload_;

  std::atomic<bool> catalog_ready_{false};
  std::atomic<bool> catalog_pending_{false};
  std::atomic<bool> stations_ready_{false};
  std::atomic<bool> stations_pending_{false};
  std::atomic<bool> sound_busy_{false};
  // Retry deadlines in 64-bit milliseconds (millis_64()): a 32-bit deadline goes stale after 24.8 days.
  uint64_t next_catalog_try_{0};
  uint64_t next_stations_try_{0};
  uint64_t next_default_try_[NUM_LISTS]{};  // automatic install of an empty slot: at most every 30 min

  // playback of stored sounds
  int playing_slot_{-1};
  uint32_t playing_since_{0};
  bool playing_seen_{false};

  // radio
  int radio_station_{-1};
  bool radio_subscribed_{false};
  uint32_t radio_token_{0};
  uint32_t radio_started_{0};
  uint8_t radio_retries_{0};
  bool radio_streaming_{false};

  // prayer times (main loop)
  std::string location_;
  std::string loaded_key_;
  int loaded_year_{0};
  std::vector<uint16_t> cur_table_;
  std::vector<uint16_t> prev_table_;
  TzInfo tz_{};
  bool tz_valid_{false};
  std::string tz_text_;
  bool standin_{false};
  uint32_t schedule_version_{1};
  int last_doy_{-1};
  bool prayer_job_pending_{false};
  bool cur_not_published_{false};
  bool force_refresh_{false};
  uint64_t next_cur_try_{0};
  uint64_t next_prev_try_{0};
  uint64_t next_next_try_{0};
  uint32_t last_tick_{0};
};

}  // namespace athan
}  // namespace esphome
