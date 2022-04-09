#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_WEBDATABASE_DOM_WINDOW_AUDIO_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_WEBDATABASE_DOM_WINDOW_AUDIO_H_

#include "audio_manager.h"

#include "third_party/blink/renderer/bindings/core/v8/script_promise.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_audio_callback.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"
namespace blink {

class ExceptionState;
class LocalDOMWindow;

class MODULES_EXPORT DOMWindowAudio {
  STATIC_ONLY(DOMWindowAudio);

public:
  static AudioManager* startAudioNative(LocalDOMWindow&, V8VideoCardAudioCallback*, ExceptionState&);

private:

};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_WEBDATABASE_DOM_WINDOW_AUDIO_H_

