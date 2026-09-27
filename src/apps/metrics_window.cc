#include "bupt_volumetric/apps/metrics_window.h"

#include <windows.h>

#include <string>
#include <vector>

namespace bupt::volumetric {
namespace {

constexpr const char* kClassName = "bupt_volumetric_metrics_window";

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  if (message == WM_PAINT) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    // Repaint black; the caller refreshes the text after each sample.
    RECT client;
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    EndPaint(hwnd, &paint);
    return 0;
  }
  return DefWindowProcA(hwnd, message, wparam, lparam);
}

}  // namespace

MetricsWindow::~MetricsWindow() { Destroy(); }

bool MetricsWindow::Create(const char* title, int width, int height) {
  if (hwnd_) {
    return true;
  }
  const HINSTANCE instance = GetModuleHandleA(nullptr);
  WNDCLASSA wc = {};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  wc.lpszClassName = kClassName;
  RegisterClassA(&wc);

  RECT rect = {0, 0, width, height};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  hwnd_ = CreateWindowExA(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT, CW_USEDEFAULT,
                          rect.right - rect.left, rect.bottom - rect.top,
                          nullptr, nullptr, instance, nullptr);
  if (!hwnd_) {
    return false;
  }
  width_ = width;
  height_ = height;
  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);
  return true;
}

bool MetricsWindow::DrawText(const std::string& text) {
  if (!hwnd_) {
    return false;
  }
  HDC dc = GetDC(hwnd_);
  if (!dc) {
    return false;
  }
  RECT client;
  GetClientRect(hwnd_, &client);
  FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 0));  // bright yellow

  int y = 8;
  std::size_t start = 0;
  for (;;) {
    const std::size_t newline = text.find('\n', start);
    const std::string line =
        text.substr(start, newline == std::string::npos ? std::string::npos
                                                       : newline - start);
    TextOutA(dc, 8, y, line.c_str(), static_cast<int>(line.size()));
    y += 18;
    if (newline == std::string::npos) {
      break;
    }
    start = newline + 1;
  }
  ReleaseDC(hwnd_, dc);
  return true;
}

void MetricsWindow::Destroy() {
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

}  // namespace bupt::volumetric
