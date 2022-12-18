#pragma once

#include "base/memory/scoped_refptr.h"
#include "base/task/single_thread_task_runner.h"
#include "media/base/audio_buffer.h"
#include "media/video/gpu_memory_buffer_video_frame_pool.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_audio_callback.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_frame_callback.h"
#include "third_party/blink/renderer/modules/webcodecs/video_frame.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "ui/gfx/gpu_memory_buffer.h"
#include <string>

#include "Processing.NDI.Lib.h"

namespace blink {

class NdiInputStream : public ScriptWrappable {
    DEFINE_WRAPPERTYPEINFO();
    
public:
    NdiInputStream(std::string url, V8VideoCardFrameCallback*, V8VideoCardAudioCallback* );
    void Trace(Visitor*) const override; 
    VideoFrame* getVideoFrame(ExecutionContext*);    
    void disable();
            
private:
    void startInternal();
    void processVideoFrame(NDIlib_video_frame_v2_t, base::TimeDelta);
    void processAudio(NDIlib_audio_frame_v2_t, base::TimeDelta);
    void OnAudioDataReceived(scoped_refptr<media::AudioBuffer>);
    void OnVideoFrameReceived();
    void debugFourCC(NDIlib_video_frame_v2_t video_frame);

    Member<V8VideoCardAudioCallback> audioCallback_;
    Member<V8VideoCardFrameCallback> frameCallback_;
    scoped_refptr<media::VideoFrame> videoFrame_;
    Member<VideoFrame> videoFrame;

    // TODO GC
    uint8_t *i420originalSizeY_;
    uint8_t *i420originalSizeU_;
    uint8_t *i420originalSizeV_;

    // imagen scalada para reducir uso de cpu
    uint8_t* scaledY_;
    uint8_t* scaledU_;
    uint8_t* scaledV_;
    int scaledWidth_;
    int scaledHeight_;

    // imagen en formato NV12
    uint8_t* nv12Y_;
    uint8_t* nv12UV_;
    int nv12Width_;
    int nv12Height_;
    
    // TODO GC
    uint8_t** audioDataTemp_;
    
    // timestamp del principio de la captura
    uint64_t startTimestamp_;
    int32_t frameCounter_;
    base::TimeDelta currentFrameTime_;

    std::string url_;
    scoped_refptr<base::TaskRunner> taskRunner_;
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
    bool enabled_;

    // Acceso gpu
    bool gpuPoolInitialized_;
    std::unique_ptr<media::GpuMemoryBufferVideoFramePool> gpuPool_;
    
    void retrievedGpuVideoAcceleratorFactories(media::GpuVideoAcceleratorFactories*);
    void frameReadyCB(scoped_refptr<media::VideoFrame>);
    std::unique_ptr<gfx::GpuMemoryBuffer> gpuMemoryBuffer_;
    
    scoped_refptr<base::SingleThreadTaskRunner> media_task_runner_;
    scoped_refptr<base::TaskRunner> copy_task_runner_;
    
};

}

