#ifndef BUPT_VOLUMETRIC_APPS_METRICS_WINDOW_H_
#define BUPT_VOLUMETRIC_APPS_METRICS_WINDOW_H_

#include <windows.h>

#include <string>

namespace bupt::volumetric {

// Minimal GDI window for the metrics display: a black background with a text
// overlay showing latency/throughput/FEC stats. It has no video path (no
// I420/ffmpeg) — the "frame" is intentionally blank. Modeled on bupt_pc's
// VideoWindow overlay path.
class MetricsWindow final {
 public:
  MetricsWindow() = default;
  ~MetricsWindow();

  MetricsWindow(const MetricsWindow&) = delete;
  MetricsWindow& operator=(const MetricsWindow&) = delete;

  // Creates a top-level window of the given client size.
  bool Create(const char* title, int width, int height);

  // Clears the client area to black and draws the text overlay (top-left,
  // bright yellow). Multi-line text is supported via '\n'. An empty string
  // just repaints black.
  bool DrawText(const std::string& text);

  void Destroy();

  HWND hwnd() const { return hwnd_; }

 private:
  HWND hwnd_ = nullptr;
  int width_ = 0;
  int height_ = 0;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_APPS_METRICS_WINDOW_H_
