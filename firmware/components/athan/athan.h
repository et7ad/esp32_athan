#pragma once
// athan: the version 3 parts ESPHome does not provide.
//
//  * Four replaceable sounds in flash (athan, fajr, tawashih, tick): download from the suggested list on GitHub or
//    upload from the /audio web page; every file is checked (MP3, size, duration) in PSRAM before flash is touched.
//  * The suggested lists (docs/audio/catalog.json) and the radio stations (docs/radio/stations.json).
//  * Radio: ten slots, each either subscribed to the project's station list (kept in memory, refreshed every
//    6 h) or playing the owner's own URL.
//  * Prayer times: one yearly file per mosque (docs/athantimes/<key>/<year>.json), stored in flash, next year
//    fetched from 1 December, last year's times as a stand-in until the new year is published, runtime time zone.
//
// Everything slow (HTTPS, flash erase/write, JSON parsing) runs on one worker task. Results come back to the main
// loop through Component::defer(), which is safe to call from other tasks.
//
// The yaml (firmware/athan.yaml) calls the public methods below from lambdas; see DEVELOPER.md for the full API.

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "esphome/components/speaker/media_player/speaker_media_player.h"
#include "esphome/components/text/text.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "audio_slots.h"
#include "menu.h"
#include "prayer_store.h"
#include "tz_posix.h"

namespace esphome {
namespace athan {

static const int NUM_LISTS = 4;  // athan, fajr, tawashih, tick: list i fills slot i
static const int NUM_STATIONS = 10;
static const int MAX_ENTRIES = 10;
// Positions in the device menu's sound lists that are not suggested-list entries (see menu_item()).
static const int MENU_NONE = -3;    // nothing there (list not loaded yet)
static const int MENU_OFF = -2;     // "Off", first in the tick list
static const int MENU_CUSTOM = -1;  // the slot's own sound when it is not in the suggested list (an upload)
static const int MENU_RANDOM = -4;  // "Random", in the tawashih list: one entry a day, streamed, nothing stored

struct NamedUrl {
  std::string name;
  std::string url;
};

class AudioWebHandler;

class AthanComponent : public Component {
 public:
  void set_media_player(speaker::SpeakerMediaPlayer *player) { this->player_ = player; }
  /// The first speaker of each pipeline (the resamplers). Starts wait until it has stopped, and the format it was
  /// given is checked against the sound's real one (see "clean starts" in athan.cpp).
  void set_media_speaker(speaker::Speaker *s) { this->media_speaker_ = s; }
  void set_announcement_speaker(speaker::Speaker *s) { this->announce_speaker_ = s; }
  void set_time(time::RealTimeClock *time) { this->time_ = time; }
  void set_data_url(const std::string &url) { this->data_url_ = url; }
  /// Wi-Fi 802.11b/g only, without 802.11n (its aggregated bursts and block acknowledgments): the setting that
  /// cured ESP devices being dropped by TP-Link routers. Applied before every association.
  void set_wifi_bg_only(bool on) { this->wifi_bg_only_ = on; }
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
  /// A built-in tone (the yaml's tone_click / tone_volume) on the announcement pipeline, started cleanly like
  /// every other sound: never on a pipeline still playing a sound of another format.
  void play_tone(audio::AudioFile *file);
  /// Stop the announcement pipeline (athan, tawashih, tick, tone) and drop a start still waiting for it.
  void stop_announcements();
  /// Wi-Fi without modem sleep: faster stream starts and steadier streaming. Only while Bluetooth is off (the
  /// yaml calls it after ble.disable, and with false before ble.enable): ESP-IDF needs modem sleep with Bluetooth.
  void set_wifi_low_latency(bool on);
  /// True while a stored sound started by play_slot() is playing.
  bool slot_playing() const { return this->playing_slot_ >= 0; }
  /// Download entry `entry` of the suggested list into `slot` (checks first). The selected entry is not
  /// downloaded again (to refresh it, download another entry, then this one).
  void download_from_catalog(int slot, int entry);
  /// Index of the suggested-list entry selected in `slot` (stored there), -1 if the slot's sound is not listed.
  int selected_entry(int slot) const;
  /// The slot holds a sound that is not in the suggested list: an upload, or a download the list no longer has.
  /// Menus show it as "Custom", so it can be previewed and kept.
  bool has_custom(int slot) const;
  /// A device-menu sound list: "Off" first for the tick (`with_off`), "Random" first in the tawashih list, then
  /// "Custom" while has_custom(), then the suggested entries. menu_item() says what sits at
  /// `index` (an entry, MENU_OFF, MENU_RANDOM, MENU_CUSTOM or MENU_NONE); menu_index() is the reverse (0 when
  /// `item` is not in the list).
  int menu_size(int slot, bool with_off) const;
  int menu_item(int slot, bool with_off, int index) const;
  int menu_index(int slot, bool with_off, int item) const;
  bool sound_busy() const { return this->sound_busy_.load(); }
  std::string sound_status() const;

  // ---------------- random tawashih ----------------
  /// Pre-Fajr plays a random entry of the tawashih list, a new one each day, streamed (nothing stored). The default.
  /// Saved across restarts. Turning it on frees the stored tawashih (its slot is emptied once nothing plays from it);
  /// a tawashih downloaded or uploaded later turns it off.
  bool tawashih_random() const { return this->tawashih_random_.load(); }
  void set_tawashih_random(bool on);
  /// Pre-Fajr with Random: streams a random list entry (another than last time). False when it cannot start (no
  /// internet, list not loaded): nothing plays then.
  bool play_random_tawashih();
  /// The random tawashih is starting or playing.
  bool tawashih_streaming() const { return this->tawashih_stream_; }
  /// Stops the random tawashih (Up/Down/Select while it plays). Nothing else.
  void stop_tawashih();

  // ---------------- suggested lists ----------------
  void fetch_catalog();
  bool catalog_ready() const { return this->catalog_ready_.load(); }
  int catalog_size(int list) const;
  std::string catalog_name(int list, int entry) const;
  /// Listen to an entry, nothing stored: the selected entry plays from flash, any other streams (media pipeline).
  void preview_catalog(int list, int entry);
  /// Preview the sound stored in `slot` from flash (the "Custom" item), nothing stored or changed.
  void preview_stored(int slot);
  /// How menus and the /audio page start a preview: `item` is a list entry or MENU_CUSTOM. Whatever previews now
  /// stops at once (a single STOP), and the new preview starts after `delay_ms` without another request. Browsing
  /// quickly therefore opens one stream when you stop, not one per press, and never floods the media player:
  /// its command queue (20) is drained one per loop by this same main loop, which fills it with a blocking send,
  /// so a full queue would freeze the loop until the task watchdog resets the board.
  void request_preview(int list, int item, uint32_t delay_ms);
  /// Called right before a preview starts (menu or /audio page) with its list, so the yaml can set the volume:
  /// Fajr volume for lists 1 (fajr) and 2 (tawashih), the normal volume for 0 (athan) and 3 (tick).
  void set_preview_volume_callback(std::function<void(int)> cb) { this->preview_volume_cb_ = std::move(cb); }
  /// Called when a stop the owner asked for should be silent at once (a preview left while browsing, the /audio
  /// page's Stop): the yaml mutes the output while the pipelines stop (hard_mute).
  void set_hard_stop_callback(std::function<void()> cb) { this->hard_stop_cb_ = std::move(cb); }
  /// hard_mute: called when it mutes the output. audio_drained() then says when every pipeline that was playing
  /// has stopped and played out (its resampler stops only once its mixer input has), so the unmute leaves no tail.
  void begin_drain_watch();
  bool audio_drained() const;
  /// List being previewed (streaming or from flash), -1 if none.
  int preview_list() const { return this->preview_list_; }
  /// Stop a preview (and one still waiting to start) at once, silently (hard stop). The radio plays on when no
  /// preview had replaced it: what the device menu calls when it leaves a sound row.
  void stop_preview();
  /// Stop the radio and any preview (also a preview playing from flash). Never stops the athan or the tick.
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
  /// Prayer times of the selected location are stored (any year). The screen shows the stored next prayer, struck
  /// through, until the clock is set.
  bool has_stored_times() const { return this->store_.ready() && this->store_.has_any(this->location_); }
  /// Download the current year again (the "Refresh Prayer Times" button).
  void refresh_prayer_times();
  std::string prayer_status() const;

  // ---------------- Wi-Fi drops (diagnostics for the web page) ----------------
  /// Connections lost since start. Changes whenever wifi_drops_text() does.
  uint32_t wifi_drop_count() const { return this->wifi_drop_count_; }
  /// "3 since start: 01:30 not authenticated (reason 6, -44 dBm); 00:50 ..." (the last three, newest first).
  std::string wifi_drops_text() const;

  // ---------------- device menu ----------------
  /// The OLED menu's navigation (menu.h). Its rows are added by the yaml (script menu_setup); draw it with
  /// draw_menu() (menu_view.h). Main loop only.
  Menu &menu() { return this->menu_; }

  // ---------------- used by the /audio web page (httpd task) ----------------
  enum class WebAction : uint8_t { DOWNLOAD, PREVIEW, STOP, RANDOM };
  void web_action(WebAction action, int list, int entry);
  bool upload_begin(int slot, const std::string &filename);
  void upload_data(const uint8_t *data, size_t len);
  void upload_end();
  /// The page (about 14 KB) and its status bar are built in PSRAM: a page view never takes internal RAM.
  PsramString render_audio_page();
  /// The status bar of the /audio page (an iframe, so actions never reload or scroll the page). `gen` is the
  /// sounds_changed_ value the page was drawn with: when it differs, the bar offers a reload.
  PsramString render_audio_status(uint32_t gen);

 protected:
  enum class JobType : uint8_t {
    CATALOG,
    STATIONS,
    STATION_PLAY,
    DOWNLOAD_URL,
    COMMIT_BUFFER,
    PRAYER_YEAR,
    CHECK_STREAM,
    CLEAR_SLOT
  };
  enum class PrayerPurpose : uint8_t { CURRENT, PREVIOUS, NEXT };
  // NO_CONNECTION: the server was not reached at all (no network, DNS, refused, TLS); FAILED: any other error.
  enum class HttpResult : uint8_t { OK, NOT_FOUND, TOO_BIG, FAILED, NO_MEMORY, NO_CONNECTION };
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
  /// `coalesce`: a job of the same type still waiting in the queue is replaced instead of queuing another, so
  /// repeated presses cost one job.
  void enqueue_(Job &&job, bool coalesce = false, bool front = false);
  bool prayer_first_(uint64_t now64) const;
  void run_job_(Job &job);
  /// `truncate`: read the first `max_len` bytes and stop (a live stream, or the start of a file) instead of
  /// refusing a longer body.
  HttpResult http_get_(const std::string &url, size_t max_len, uint8_t **out, size_t *out_len, int progress_slot,
                       bool truncate = false);
  void job_check_stream_(Job &job);
  void job_catalog_();
  void job_stations_(Job &job, bool for_play);
  void job_download_url_(Job &job);
  void job_commit_(int slot, uint8_t *data, size_t len, const std::string &label, uint32_t source = 0);
  void job_prayer_(Job &job);
  bool parse_stations_(const uint8_t *data, size_t len);

  // main loop helpers
  void prayer_tick_(bool online);
  void load_tables_(int year);
  void apply_tz_(const std::string &tz);
  void radio_tick_();
  void net_watch_();
  void tune_tx_power_();
  void start_stream_(const std::string &url);
  void start_announcement_(audio::AudioFile *file);
  void pump_starts_();
  void check_formats_();
  void check_stream_now_();
  bool media_chain_stopped_() const;
  bool announce_chain_stopped_() const;
  void stop_slot_preview_();
  bool begin_preview_(int list);
  bool preview_from_flash_(int list, const std::string &name);
  // The only places that send STOP: each one only when something may be playing, so repeated presses or
  // timeouts never queue a STOP per call.
  void stop_media_pipeline_();
  void set_sound_status_(const std::string &s);
  void set_radio_status_(const std::string &s);
  void set_prayer_status_(const std::string &s);
  std::string resolve_url_(const std::string &base, const std::string &url) const;

  speaker::SpeakerMediaPlayer *player_{nullptr};
  speaker::Speaker *media_speaker_{nullptr};
  speaker::Speaker *announce_speaker_{nullptr};
  time::RealTimeClock *time_{nullptr};
  std::string data_url_;
  bool wifi_bg_only_{false};
  std::vector<text::Text *> radio_urls_;

  AudioSlots slots_;
  PrayerStore store_;
  Menu menu_;
  // What the /audio page shows of each slot. The page renders on the web server's task while the main loop rewrites
  // slots, so it reads this copy (under mutex_), which only the main loop refreshes (refresh_slot_view_()): after
  // boot, a catalog load, and before and after every rewrite.
  struct SlotView {
    bool valid{false};
    bool custom{false};
    int selected{-1};
    uint32_t duration_ms{0};
    std::string label;
  };
  SlotView slot_view_[NUM_SLOTS];
  void refresh_slot_view_();
  void job_clear_slot_(Job &job);
  // The last Wi-Fi drops (main loop; the reason comes from an ESP-IDF event handler).
  struct WifiDrop {
    char when[6];
    uint8_t reason;
    int8_t rssi;
  };
  WifiDrop wifi_drops_[3]{};  // newest first
  uint32_t wifi_drop_count_{0};
  void record_wifi_drop_();
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
  std::atomic<bool> web_actions_pending_{false};  // loop() looks at web_actions_ only when set
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
  // Random tawashih (set_tawashih_random()). gen_ counts the times it was chosen: a tawashih download or upload
  // started before the last choice (job_gen_ older) does not turn it off when it is stored, and is freed instead.
  std::atomic<bool> tawashih_random_{true};
  ESPPreferenceObject random_pref_;
  std::atomic<uint32_t> tawashih_gen_{0};
  std::atomic<uint32_t> tawashih_job_gen_{0};  // set on the web server's task too (an upload)
  bool clear_tawashih_{false};       // free the tawashih slot as soon as nothing plays from it
  bool tawashih_stream_{false};      // the random tawashih is starting or playing (media pipeline)
  bool tawashih_retried_{false};
  uint32_t tawashih_started_{0};
  int tawashih_last_{-1};            // entry played last time (not repeated the next day)
  std::atomic<uint32_t> sounds_changed_{0};  // +1 after every download/upload that changed a slot (/audio reload hint)
  // Retry deadlines in 64-bit milliseconds (millis_64()): a 32-bit deadline goes stale after 24.8 days.
  uint64_t next_catalog_try_{0};
  uint64_t next_stations_try_{0};
  uint64_t stations_fetched_at_{0};  // millis_64() of the last good station list
  uint64_t next_default_try_[NUM_LISTS]{};  // automatic download into an empty slot: at most every 30 min

  // playback of stored sounds
  int playing_slot_{-1};
  uint32_t playing_since_{0};
  bool playing_seen_{false};
  std::function<void(int)> preview_volume_cb_;
  std::function<void()> hard_stop_cb_;
  int preview_list_{-1};
  uint32_t preview_started_{0};
  bool slot_preview_{false};  // the stored sound playing is a preview (stop_media() stops it, a download may start)
  bool slot_stop_sent_{false};  // STOP already sent for that preview
  bool media_started_{false};   // a stream (radio or preview) was started on the media pipeline since the last STOP
  bool media_stop_pending_{false};  // a STOP was sent to a playing media pipeline: stored sounds wait for it
  uint32_t media_stop_sent_at_{0};
  // Stops of each pipeline, counted in loop() (a resampler that went from running to stopped)
  uint32_t media_stops_{0};
  uint32_t announce_stops_{0};
  bool media_was_stopped_{true};
  bool announce_was_stopped_{true};
  // begin_drain_watch(): the pipelines that were playing, and their stop counts then
  bool drain_media_{false};
  bool drain_announce_{false};
  uint32_t drain_media_stops_{0};
  uint32_t drain_announce_stops_{0};
  int pending_preview_list_{-1};  // request_preview(): what to start, and when
  int pending_preview_item_{0};
  uint32_t pending_preview_at_{0};

  // Clean starts (pump_starts_()): what waits for its pipeline to stop, and the backstop format checks after a start.
  bool stream_pending_{false};      // waits for its pipeline to stop
  std::string stream_url_;          // the media pipeline's current (or pending) stream
  uint32_t stream_pending_since_{0};
  uint32_t stream_token_{0};        // +1 per stream start: a check of an older start is ignored
  uint8_t stream_restarts_{0};      // restarts for a wrong format, per stream (at most 3)
  bool fmt_check_running_{false};   // a CHECK_STREAM is queued or running (one at a time)
  bool fmt_reverify_{false};        // the last check disagreed with what plays: one more before acting
  uint32_t fmt_next_check_{0};      // next check of the playing stream (10 s after a start, then every 10 min)
  audio::AudioFile *announce_pending_{nullptr};
  uint32_t announce_pending_since_{0};
  bool announce_stop_sent_{false};
  int announce_check_slot_{-1};     // stored sound whose pipeline format is still to be checked
  uint32_t announce_started_at_{0};
  bool announce_restarted_{false};

  // radio
  int radio_station_{-1};
  bool radio_subscribed_{false};
  uint32_t radio_token_{0};
  uint32_t radio_started_{0};
  uint8_t radio_retries_{0};
  bool radio_waiting_net_{false};  // the radio is on but paused until the network is back (net_watch_())
  uint32_t radio_wait_since_{0};
  bool online_{false};             // network state net_watch_() saw last
  uint64_t online_since_{0};       // when the network came up (net_watch_())

  // prayer times (main loop)
  std::string location_;
  std::string loaded_key_;
  int loaded_year_{0};
  PsramVector<uint16_t> cur_table_;  // 8.8 KB each, in PSRAM
  PsramVector<uint16_t> prev_table_;
  TzInfo tz_{};
  bool tz_valid_{false};
  std::string tz_text_;
  bool standin_{false};
  uint32_t prayer_status_key_{UINT32_MAX};  // what the prayer status line was built from (prayer_tick_())
  uint32_t schedule_version_{1};
  int last_doy_{-1};
  bool prayer_job_pending_{false};
  bool cur_not_published_{false};
  bool prayer_tried_{false};       // a download of this year's (or last year's) times has run since boot
  bool force_refresh_{false};
  uint64_t next_cur_try_{0};
  uint64_t next_prev_try_{0};
  uint64_t next_next_try_{0};
  uint32_t last_tick_{0};
  uint32_t last_tx_tune_{0};  // tune_tx_power_(), every 10 s
};

}  // namespace athan
}  // namespace esphome
