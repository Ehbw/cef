#ifndef LIBCEF_BROWSER_OSR_VIDEO_CONSUMER_OSR_H_
#define LIBCEF_BROWSER_OSR_VIDEO_CONSUMER_OSR_H_

#include <optional>
#include <mutex>
#include <unordered_map>

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

  uint32_t current_seq = 0;
  uint32_t sequence = 0;

  std::unordered_map<uint32_t, mojo::Remote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks>> callbacks;

  mojo::Remote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks> pending_callback;

  cef_lock_frame_info_t last_info = {sizeof(cef_lock_frame_info_t)};

  // Keep in sync with cef_lock_frame_info_t::dirty_rects in cef_types.h
  static constexpr int kMaxDirtyRects = 10;
  // Chromium has a max of 11 inflight frames, we leave two spare so chromium doesn't freak out if we have used all frames.
  static constexpr int kMaxInflightFrames = 8;

  // Used with watchdog to detect when a frame has stopped being alive, only for PET_VIEW
  base::TimeTicks last_frame_update_time;

  bool had_backlog;

  int dirty_rects_count = 0;
  cef_rect_t dirty_rects[kMaxDirtyRects]; 

  ~FrameSlot() { 
      for (auto& [seq, cb] : callbacks) {
        if (cb) {
          cb->Done();
        }
      }
      callbacks.clear();

      if (pending_callback) {
        pending_callback->Done();
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
  void ReleaseFrame(cef_paint_element_type_t type, int sequence_id);
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

  void Watchdog();

  // CFX: Lockframe patch
  base::RepeatingTimer watchdog_;

  std::mutex frame_mutex_;
  FrameSlot slots_[2];
};

#endif  // LIBCEF_BROWSER_OSR_VIDEO_CONSUMER_OSR_H_
