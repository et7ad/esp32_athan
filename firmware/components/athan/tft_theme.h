#pragma once
// The colour TFT's look (displays/tft.yaml, menu_view_tft.cpp): its colours and the icon font's code points. Plain
// constants with no ESPHome types, so menu.h (host-tested) can name a row's icon. The OLED build ignores them.

#include <cstdint>

namespace esphome {
namespace athan {
namespace tft {

// Neutrals: white and greys on black. A backlit panel glows wherever it is lit, so large areas stay black.
constexpr uint32_t FG = 0xF2F2EF;     // text
constexpr uint32_t SUB = 0xA2A8AE;    // secondary text: the date, captions, prayers to come
constexpr uint32_t DIM = 0x5D646A;    // prayers that have passed, rows two away in the menu
constexpr uint32_t LINE = 0x2C3136;   // lines, frames, empty bars
constexpr uint32_t CARD = 0x121518;   // pop-ups, the Wi-Fi setup boxes
constexpr uint32_t TRACK = 0x1E2226;  // the ring's empty part
constexpr uint32_t INK = 0x0B0C0D;    // text on a coloured fill
constexpr uint32_t WARN = 0xF0A040;   // times borrowed from last year; "Press Select"
constexpr uint32_t BAD = 0xF2685C;    // no Wi-Fi

// The accent marks what matters now (the next prayer, the cursor, the time left); the athan has a colour of its own.
// The owner picks the pair (Screen Colour): the names are the web select's options, in this order.
struct Palette {
  const char *name;
  uint32_t accent, athan;
};
constexpr int NUM_PALETTES = 5;
constexpr Palette PALETTES[NUM_PALETTES] = {
    {"Gold", 0xF2B544, 0x43C98B}, {"Teal", 0x2CC7B6, 0xF2B544}, {"Green", 0x46C46E, 0xF2B544},
    {"Blue", 0x5AAEFF, 0xF2B544}, {"White", 0xEDEDEB, 0xEDEDEB},
};
constexpr int DEFAULT_PALETTE = 1;  // Teal

inline const Palette &palette(int i) { return PALETTES[(i >= 0 && i < NUM_PALETTES) ? i : DEFAULT_PALETTE]; }

}  // namespace tft

// Material Symbols Rounded (Google's open icon font) code points, UTF-8. The icon fonts in displays/tft.yaml list
// the same code points (tests/run_tests.sh checks it): a glyph missing there draws nothing.
namespace sym {
// menu rows (MenuRow::symbol)
constexpr const char *RADIO = "\ue03e";
constexpr const char *MOSQUE = "\ueab2";
constexpr const char *TWILIGHT = "\ue1c6";       // wb_twilight: Fajr Athan
constexpr const char *QUEUE_MUSIC = "\ue03d";    // Tawashih
constexpr const char *ALARM = "\ue855";          // Pre-Fajr
constexpr const char *TIMER = "\ue425";          // Hourly Tick
constexpr const char *SCHEDULE = "\uefd6";       // Tick From, Tick Until
constexpr const char *TOGGLE_ON = "\ue9f6";      // Athan On/Off
constexpr const char *VOLUME_UP = "\ue050";      // Volume, Fajr Volume; the athan; the volume pop-up
constexpr const char *LOCATION = "\uf1db";       // location_on
constexpr const char *CLOCK = "\uf8bb";          // nest_clock_farsight_analog: Clock
constexpr const char *BRIGHTNESS = "\ue3ab";     // brightness_6
constexpr const char *DARK_MODE = "\ue51c";      // Screen at Night
constexpr const char *BEDTIME = "\uf159";        // Night From, Night Until
constexpr const char *PALETTE = "\ue40a";        // Screen Colour
constexpr const char *SYSTEM_UPDATE = "\uf2cd";  // Update
constexpr const char *LOCK = "\ue899";           // Lock Buttons; the locked mark
constexpr const char *INFO = "\ue88e";
constexpr const char *ARROW_BACK = "\ue5c4";     // Exit
// everything else
constexpr const char *CHEVRON_LEFT = "\ue5cb";
constexpr const char *CHEVRON_RIGHT = "\ue5cc";
constexpr const char *CHECK = "\ue668";
constexpr const char *CHECK_CIRCLE = "\uf0be";   // the item in use
constexpr const char *GRAPHIC_EQ = "\ue1b8";     // the item playing
constexpr const char *VOLUME_OFF = "\ue04f";     // a prayer whose athan is off
constexpr const char *WIFI = "\ue63e";
constexpr const char *WIFI_OFF = "\ue648";
constexpr const char *HISTORY = "\ue8b3";        // times borrowed from last year
constexpr const char *BLUETOOTH = "\ue1a7";
constexpr const char *HOTSPOT = "\ue1e2";        // wifi_tethering
constexpr const char *POWER = "\uf8c7";          // power_settings_new: the relay pop-up
}  // namespace sym

}  // namespace athan
}  // namespace esphome
