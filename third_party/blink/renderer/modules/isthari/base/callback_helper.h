#pragma once

#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_audio_callback.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_frame_callback.h"

namespace blink {
    
bool isAvailableVideoFrameCallback(V8VideoCardFrameCallback* );
bool isAvailableAudioDataCallback(V8VideoCardAudioCallback* );

}