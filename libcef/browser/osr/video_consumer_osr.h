#ifndef LIBCEF_BROWSER_OSR_VIDEO_CONSUMER_OSR_H_
#define LIBCEF_BROWSER_OSR_VIDEO_CONSUMER_OSR_H_

#include <optional>
#include <mutex>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "components/viz/host/client_frame_sink_video_capturer.h"
#include "media/capture/mojom/video_capture_types.mojom.h"
#include "cef/include/internal/cef_types.h"
#include "cef/include/internal/cef_types_win.h"

class CefRenderWidgetHostViewOSR;

// CFX: LockFrame implementation
struct FrameSlot {
  // Current Frame
  gfx::GpuMemoryBufferHandle current;
  // Future Frame
  gfx::GpuMemoryBufferHandle pending;

  HANDLE last_handle = nullptr;

  // This runner can be re-used.
  scoped_refptr<base::SequencedTaskRunner> runner;

  // Callback for the current frame.
  mojo::Remote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks> current_callback;
  // Callback for the previous frame.
  mojo::Remote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks> previous_callback;
  // Callback for the future frame. null when the next frame isn't ready yet.
  mojo::Remote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks> pending_callback;

  cef_lock_frame_info_t last_info = {sizeof(cef_lock_frame_info_t)};

  // Keep in sync with cef_lock_frame_info_t::dirty_rects in cef_types.h
  static constexpr int kMaxDirtyRects = 10;

  int dirty_rects_count = 0;
  cef_rect_t dirty_rects[kMaxDirtyRects]; 

  ~FrameSlot() { 
      if (!runner) {
        return; 
      }

      if (current_callback) {
          runner->PostTask(
              FROM_HERE,
              base::BindOnce([](decltype(current_callback) c) { 
                  c->Done(); 
              }, std::move(current_callback)));
      }

      if (previous_callback) {
          runner->PostTask(
              FROM_HERE,
              base::BindOnce([](decltype(previous_callback) c) { 
                  c->Done(); 
              }, std::move(previous_callback)));
      }

      if (pending_callback) {
          runner->PostTask(
              FROM_HERE,
              base::BindOnce([](decltype(pending_callback) c) { 
                  c->Done(); 
              }, std::move(pending_callback)));
      }

      // GpuMemoryBufferHandle dtor releases the DXGI handles.
  }

  void PushRect(const gfx::Rect& rect) {
    if (dirty_rects_count < kMaxDirtyRects) {
      dirty_rects[dirty_rects_count++] = {rect.x(), rect.y(), rect.width(), rect.height()};
    }
  }

  void ClearRects() {
    dirty_rects_count = 0;
    memset(&dirty_rects, 0, sizeof(cef_rect_t) * kMaxDirtyRects);
  }
};
//

class CefVideoConsumerOSR : public viz::mojom::FrameSinkVideoConsumer {
 public:
  CefVideoConsumerOSR(CefRenderWidgetHostViewOSR* view,
                      bool use_shared_texture);

  CefVideoConsumerOSR(const CefVideoConsumerOSR&) = delete;
  CefVideoConsumerOSR& operator=(const CefVideoConsumerOSR&) = delete;

  ~CefVideoConsumerOSR() override;

  void SetActive(bool active);
  void SetFrameRate(base::TimeDelta frame_rate);
  void SizeChanged(const gfx::Size& size_in_pixels);
  void RequestRefreshFrame(const std::optional<gfx::Rect>& bounds_in_pixels);
 
  // CFX: Lockframe patch
  void* LockFrame(cef_paint_element_type_t type);
  bool ReleaseFrame(cef_paint_element_type_t type);
  //

 private:
  // viz::mojom::FrameSinkVideoConsumer implementation.
  void OnFrameCaptured(
      media::mojom::VideoBufferHandlePtr data,
      media::mojom::VideoFrameInfoPtr info,
      const gfx::Rect& content_rect,
      mojo::PendingRemote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks>
          callbacks) override;
  void OnFrameWithEmptyRegionCapture() override {}
  void OnStopped() override {}
  void OnLog(const std::string& message) override {}
  void OnNewCaptureVersion(
      const media::CaptureVersion& capture_version) override {}

  const bool use_shared_texture_;

  const raw_ptr<CefRenderWidgetHostViewOSR> view_;
  std::unique_ptr<viz::ClientFrameSinkVideoCapturer> video_capturer_;

  gfx::Size size_in_pixels_;
  std::optional<gfx::Rect> bounds_in_pixels_;

  // CFX: Lockframe patch
  std::mutex frame_mutex_;
  FrameSlot slots_[2];
};

#endif  // LIBCEF_BROWSER_OSR_VIDEO_CONSUMER_OSR_H_
