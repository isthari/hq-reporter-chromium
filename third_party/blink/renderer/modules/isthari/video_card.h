#pragma once

#include "video_card_mode.h"

#include <list>
#include <map>
#include <string>
#include "base/threading/thread_task_runner_handle.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_array_buffer.h"
#include "third_party/blink/renderer/platform/heap/handle.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_frame_callback.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_audio_callback.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/decklink/linux/DeckLinkAPI.h"
#include "third_party/blink/renderer/modules/webcodecs/video_frame.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace blink {

class VideoCard : public ScriptWrappable,
       public IDeckLinkInputCallback
{		  
    DEFINE_WRAPPERTYPEINFO();
public:
    VideoCard(IDeckLink* deckLink_);
    void Trace(Visitor*) const override;

    // atributos solo lectura
    bool input() { return isInput_; }
    bool output() { return isOutput_; }
    String modelName() { return String(modelName_); }
    int64_t persistentId() { return persistentId_; }

    // funciones
    void enableVideoInput(ExecutionContext*, long mode, V8VideoCardFrameCallback *, V8VideoCardAudioCallback *);
    VideoFrame* getVideoFrame(ExecutionContext*);
    void disableVideoInput();
    long getModeCount() { return modes_.size(); }
    VideoCardMode* getMode(long index); 

    // IDeckLinkInputCallback
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
    ULONG STDMETHODCALLTYPE  Release(void) override { return 0; }
    HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode*, BMDDetectedVideoInputFormatFlags) override;
    HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame*, IDeckLinkAudioInputPacket*) override;


private:
    IDeckLink* deckLink_;
    IDeckLinkOutput *deckLinkOutput_;
    IDeckLinkInput *deckLinkInput_;

    std::map<int, IDeckLinkDisplayMode*> displayModes_;
    std::list<VideoCardMode *>modes_;

    bool isInput_;
    bool isOutput_;
    std::string modelName_;
    int64_t persistentId_;

    Member<ExecutionContext> executionContext_;
    Member<V8VideoCardFrameCallback> frameCallback_;
    Member<V8VideoCardAudioCallback> audioCallback_;
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
    Member<DOMArrayBuffer> buffer0;
    Member<DOMArrayBuffer> buffer1;    
    Member<VideoFrame> videoFrame;
    uint8_t *frameData0;
    uint8_t *frameData1;
    int frameCounter_;

    uint8_t** audioData0;
    uint8_t** audioData1;
    int audioCounter_;
    uint64_t audioStart_;

private:
    void checkIO();
    void getDisplayModes();

    // callbacks para ejecutar en el main thread
    void OnVideoFrameReceived();
    void OnAudioFrameReceived(int samples);
};

} // namespace blink
