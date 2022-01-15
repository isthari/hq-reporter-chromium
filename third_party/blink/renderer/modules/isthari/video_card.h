#pragma once

#include <list>
#include <map>
#include <string>
#include "base/threading/thread_task_runner_handle.h"
#include "third_party/blink/renderer/platform/heap/handle.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_frame_callback.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/decklink/linux/DeckLinkAPI.h"

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
    bool input() { return isInput_; }
    bool output() { return isOutput_; }
    String modelName() { return String(modelName_); }
    int64_t persistentId() { return persistentId_; }
    void enableVideoInput(V8VideoCardFrameCallback *callback);

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
    std::list<std::string >modes_;

    bool isInput_;
    bool isOutput_;
    std::string modelName_;
    int64_t persistentId_;

    Member<V8VideoCardFrameCallback> frameCallback_;
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;

private:
    void checkIO();
    void getDisplayModes();

    // callbacks para ejecutar en el main thread
    void OnVideoFrameReceived();
};

} // namespace blink
