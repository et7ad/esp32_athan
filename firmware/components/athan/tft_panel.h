#pragma once
// The colour TFT's start-up commands, sent again while the clock runs (displays/tft.yaml). A screen unplugged and
// plugged back in powers up asleep with its display off and stays black: ESPHome's mipi_spi driver sends these
// commands (power and gamma settings, colour format, inversion, Sleep Out, Display On, then MADCTL for the colour
// order) only in its setup(), and discards the sequence there. The clock cannot ask the panel whether it is there (SPI
// goes one way on this module), so they go blind with a whole frame: a screen that never left gets the values it
// already has, and nothing changes on it. The reset that setup() starts with is left out, because it would blank a
// working screen.
//
// Never run the driver's setup() again instead: the commands are gone by then (it resets the panel and sends nothing,
// so the screen stays black), and every run allocates a new 115 KB frame buffer without freeing the old one.
//
// The driver has no public way to send a command: PanelAccess reaches its protected members.

#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(ESP_PLATFORM) || defined(USE_ESP32)
#include "esphome/core/defines.h"
#endif
#ifdef ATHAN_DISPLAY_TFT
#include "esphome/components/mipi_spi/mipi_spi.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#endif

namespace esphome {
namespace athan {
namespace tft {

constexpr uint8_t PANEL_SLEEP_OUT = 0x11;  // MIPI DCS, as in mipi_spi.h
constexpr uint8_t PANEL_PAUSE = 0xFF;      // a start-up sequence's [ms, 0xFF]: a pause
// After Sleep Out the panel needs 5 ms before the next command; ESPHome's own sequence waits 10.
constexpr uint32_t PANEL_WAKE_MS = 10;

/// The parts of a start-up sequence: the commands before its Sleep Out (the panel's settings), Sleep Out itself, and
/// those after it (Display On). A sequence without a Sleep Out is all SETTINGS.
enum class PanelPart : uint8_t { SETTINGS, SLEEP_OUT, AFTER_WAKE };

/// Calls send(cmd, args, n) for each command of one part of a mipi_spi start-up sequence, in order ([cmd, n, args...]
/// per command, n's top bit a flag; [ms, 0xFF] per pause). The pauses are left out: they follow the reset, which is
/// not repeated, and PanelResend keeps the one after Sleep Out. False when the sequence is malformed (the commands
/// before that point have been sent). Host-tested (tests/test_helpers.cpp).
template<typename F> bool panel_part(const std::vector<uint8_t> &seq, PanelPart part, F &&send) {
  PanelPart at = PanelPart::SETTINGS;
  size_t i = 0;
  while (i < seq.size()) {
    if (seq.size() - i < 2)
      return false;
    const uint8_t cmd = seq[i], len = seq[i + 1];
    i += 2;
    if (len == PANEL_PAUSE)
      continue;
    const size_t n = len & 0x7F;
    if (seq.size() - i < n)
      return false;
    if (at == PanelPart::SETTINGS && cmd == PANEL_SLEEP_OUT) {
      at = PanelPart::SLEEP_OUT;
    } else if (at == PanelPart::SLEEP_OUT) {
      at = PanelPart::AFTER_WAKE;
    }
    if (at == part)
      send(cmd, seq.data() + i, n);
    i += n;
  }
  return true;
}

#ifdef ATHAN_DISPLAY_TFT

/// MADCTL (colour order, mirroring) as the driver sends it with no rotation: the value its type was built with.
template<typename B, mipi_spi::PixelMode BP, bool BE, mipi_spi::PixelMode DP, mipi_spi::BusType BUS, int W, int H,
         int OW, int OH, int PW, int PH, uint16_t M, bool HR>
constexpr uint8_t panel_madctl(const mipi_spi::MipiSpi<B, BP, BE, DP, BUS, W, H, OW, OH, PW, PH, M, HR> &) {
  return static_cast<uint8_t>(M);
}

/// Reaches the driver's protected members. D is the display's own type (the TFT build's id(tft) points to one).
template<typename D> class PanelAccess : public D {
 public:
  static const std::vector<uint8_t> &sequence(D &d) { return d.*(&PanelAccess::init_sequence_); }
  static void command(D &d, uint8_t cmd, const uint8_t *args, size_t n) {
    void (D::*send)(uint8_t, const uint8_t *, size_t) = &PanelAccess::write_command_;
    (d.*send)(cmd, args, n);
  }
  /// MADCTL, which the driver sends after its sequence. Not through the driver's own reset_params_(), which logs a
  /// line each time, except with a rotation: the driver works that one out.
  static void madctl(D &d) {
    if (d.get_rotation() != display::DISPLAY_ROTATION_0_DEGREES) {
      void (D::*send)() = &PanelAccess::reset_params_;
      (d.*send)();
      return;
    }
    const uint8_t v = panel_madctl(d);
    command(d, mipi_spi::MADCTL_CMD, &v, 1);
  }
};

/// The copy of the start-up commands, taken at boot before the driver's setup() sends and discards them (tft.yaml
/// on_boot).
inline std::vector<uint8_t> &panel_sequence() {
  static std::vector<uint8_t> seq;
  return seq;
}
template<typename D> void keep_panel_sequence(D &d) { panel_sequence() = PanelAccess<D>::sequence(d); }

/// The start-up commands around one whole frame, when due. Made before the frame is drawn, it sends the settings,
/// MADCTL and Sleep Out (16 commands, 1.1 ms). When it ends (the drawing is done, and the driver sends the frame next),
/// it waits out what is left of PANEL_WAKE_MS; drawing a whole frame takes longer, so it rarely waits. Display On comes
/// once the frame is on the panel: panel_on().
template<typename D> class PanelResend {
 public:
  PanelResend(D &d, bool due) {
    if (!due)
      return;
    const auto &seq = panel_sequence();
    auto send = [&d](uint8_t cmd, const uint8_t *args, size_t n) { PanelAccess<D>::command(d, cmd, args, n); };
    if (seq.empty() || !panel_part(seq, PanelPart::SETTINGS, send)) {
      static bool warned = false;
      if (!warned)
        esph_log_w("tft", "No start-up commands kept at boot: a screen plugged back in stays black until a restart");
      warned = true;
      return;
    }
    PanelAccess<D>::madctl(d);
    panel_part(seq, PanelPart::SLEEP_OUT, send);
    this->woke_ms_ = millis();
    this->sent_ = true;
  }
  ~PanelResend() {
    const uint32_t gap = millis() - this->woke_ms_;
    if (this->sent_ && gap < PANEL_WAKE_MS)
      delay(PANEL_WAKE_MS - gap);
  }
  PanelResend(const PanelResend &) = delete;
  PanelResend &operator=(const PanelResend &) = delete;

 protected:
  uint32_t woke_ms_{0};
  bool sent_{false};
};

/// Display On (with anything else after Sleep Out) once the frame drawn with PanelResend is on the panel: before it,
/// a screen just plugged back in would show what its memory held. tft.yaml script tft_panel_on.
template<typename D> void panel_on(D &d) {
  panel_part(panel_sequence(), PanelPart::AFTER_WAKE,
             [&d](uint8_t cmd, const uint8_t *args, size_t n) { PanelAccess<D>::command(d, cmd, args, n); });
}

#endif  // ATHAN_DISPLAY_TFT

}  // namespace tft
}  // namespace athan
}  // namespace esphome
