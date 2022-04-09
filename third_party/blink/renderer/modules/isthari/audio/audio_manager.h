#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_ISTHARI_AUDIO_MANAGER_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_ISTHARI_AUDIO_MANAGER_H_

#include "audio_manager.h"

#include "third_party/blink/renderer/bindings/core/v8/script_promise.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_audio_callback.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/modules/webcodecs/audio_data.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/scheduler/public/thread.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"
namespace blink {

class ExceptionState;
class LocalDOMWindow;

class AudioManager : public ScriptWrappable {
    DEFINE_WRAPPERTYPEINFO();
public:
    AudioManager(V8VideoCardAudioCallback* callback);
    void Trace(Visitor*) const override;
    long getModeCount() { return 0; }
    
private:
    void PulseThread();    
    void SendAudio(scoped_refptr<media::AudioBuffer> audioBuffer);
    
private:
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
    std::unique_ptr<blink::Thread> thread_;
    
    Member<V8VideoCardAudioCallback> callback_;
    // TODO GC
    uint64_t start_;
    int counter_;
    int samples_;
    void *audioData_;
    uint8_t** audioDataTemp_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_ISTHARI_AUDIO_MANAGER_H_

