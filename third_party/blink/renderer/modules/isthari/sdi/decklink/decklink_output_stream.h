#pragma once

#include "../../video_card_mode.h"

#include "base/threading/thread_task_runner_handle.h"
#include "media/base/audio_buffer.h"
#include "media/base/video_frame.h"
#include "media/base/video_frame_pool.h"
#include "third_party/blink/renderer/modules/webcodecs/video_frame.h"

#if BUILDFLAG(IS_LINUX)
#include "third_party/decklink/linux/DeckLinkAPI.h"
#elif BUILDFLAG(IS_WIN)
#include "third_party/decklink/win/DeckLinkAPI.h"
#endif

namespace blink {

class DecklinkOutputStream : public ScriptWrappable {
    DEFINE_WRAPPERTYPEINFO();
public:
    DecklinkOutputStream(IDeckLinkOutput*, IDeckLinkDisplayMode*, long);    
    void Trace(Visitor*) const override;

    void putVideoFrame(VideoFrame* frame); 
    void sendBlackFrame();

private:    
    IDeckLinkOutput* decklinkOutput_;
    //IDeckLinkDisplayMode* displayMode_;

    // video data
    int width_;
    int height_;

    int frameCounter_;

    // NV12ToI420 and I420Rotate
    uint8_t* i420originalY_;
    uint8_t* i420originalU_;
    uint8_t* i420originalV_;

    // I420 escalado
    int scaledStrideY_;
    int scaledStrideU_;
    int scaledStrideV_;
    uint8_t* scaledY_;
    uint8_t* scaledU_;
    uint8_t* scaledV_;

    // frame que se envia directamente a la tarjeta decklink
    IDeckLinkMutableVideoFrame *playbackFrame_;
    media::VideoFramePool videoFramePool_;

    // audio
    long audioChannels_;
};

}