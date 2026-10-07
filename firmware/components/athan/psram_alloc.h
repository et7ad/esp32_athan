#pragma once
// Containers whose storage lives in PSRAM.
//
// ESPHome builds with CONFIG_SPIRAM_USE_CAPS_ALLOC: plain new/malloc, so every std::vector and std::string, always
// takes internal RAM, which Wi-Fi, the audio DMA and the task stacks need (and which fragments). Large or
// long-lived buffers that only the CPU touches use these instead: the prayer tables, the /audio page, the menu
// rows. They fall back to internal RAM when PSRAM is full. Never use them for DMA buffers or for data read while
// the flash cache is off. On a computer (host tests) they are the standard containers.

#include <cstddef>
#include <memory>
#include <new>
#include <string>
#include <vector>

#if defined(ESP_PLATFORM) || defined(USE_ESP32)
#include <esp_heap_caps.h>
#define ATHAN_PSRAM_ALLOC 1
#endif

namespace esphome {
namespace athan {

#ifdef ATHAN_PSRAM_ALLOC
template<class T> struct PsramAllocator {
  using value_type = T;
  PsramAllocator() = default;
  template<class U> constexpr PsramAllocator(const PsramAllocator<U> &) noexcept {}
  T *allocate(std::size_t n) {
    void *p = heap_caps_malloc_prefer(n * sizeof(T), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p == nullptr)
      std::__throw_bad_alloc();  // what std::allocator does (an abort without exceptions)
    return static_cast<T *>(p);
  }
  void deallocate(T *p, std::size_t) noexcept { heap_caps_free(p); }
};
template<class T, class U> bool operator==(const PsramAllocator<T> &, const PsramAllocator<U> &) { return true; }
template<class T, class U> bool operator!=(const PsramAllocator<T> &, const PsramAllocator<U> &) { return false; }
#else
template<class T> using PsramAllocator = std::allocator<T>;
#endif

template<class T> using PsramVector = std::vector<T, PsramAllocator<T>>;
using PsramString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

}  // namespace athan
}  // namespace esphome
