#pragma once

#include "base/task/single_thread_task_runner.h"
#include "media/base/video_frame_pool.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_array_buffer.h"
#include "third_party/blink/renderer/modules/webcodecs/video_frame.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"

#include <string>

#include "Processing.NDI.Lib.h"

namespace blink {

class NdiOutputStream : public ScriptWrappable {
    DEFINE_WRAPPERTYPEINFO();
    
public:
    NdiOutputStream(std::string label);
    void Trace(Visitor*) const override; 
    void putVideoFrame(VideoFrame*);
    void putAudioFrame(NotShared<DOMFloat32Array> , NotShared<DOMFloat32Array>);
            
private:
    void putAudioFrameInternal();

    NDIlib_audio_frame_interleaved_16s_t ndiAudioFrame_;
    NDIlib_video_frame_v2_t ndiVideoFrame_;
    NDIlib_send_instance_t sender_;
    
    std::unique_ptr<uint8_t[]> imagePar_;
    std::unique_ptr<uint8_t[]> imageImpar_;

    std::unique_ptr<uint8_t[]> imageRotatePar_;
    std::unique_ptr<uint8_t[]> imageRotateImpar_;
    
    uint8_t *audioData_;
    
    // Necesario para que no se bloque el thread principal en el envio de audio
    scoped_refptr<base::TaskRunner> taskRunner_;
    media::VideoFramePool videoFramePool_;
    
    int videoIndex_;
};

}

