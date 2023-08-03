#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_ISTHARI_NDI_MANAGER
#define THIRD_PARTY_BLINK_RENDERER_MODULES_ISTHARI_NDI_MANAGER

#include "base/timer/timer.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ndi_scan_callback.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_audio_callback.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_frame_callback.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include <map>
#include <memory>
#include <string>

#include "ndi_input_stream.h"
#include "ndi_output_stream.h"
#include "Processing.NDI.Lib.h"

namespace blink {

class NdiManager : public ScriptWrappable {
    DEFINE_WRAPPERTYPEINFO();
public:
    NdiManager();
    static NdiManager* getInstance();
    void Trace(Visitor*) const override; 
        
    void getScanedStreams(V8NdiScanCallback*);
    NdiInputStream* startInputStream(String url, V8VideoCardFrameCallback*, V8VideoCardAudioCallback* );
    NdiOutputStream* startOutputStream(String label);
        
private:
    inline static NdiManager* instance_ = nullptr;  
    
    base::RepeatingTimer scanTimer_;
    NDIlib_find_instance_t find_;
    std::shared_ptr<std::map<std::string, std::string>> scanedStreams_; 
    
private:
    void scanCallback();
};

}

#endif // THIRD_PARTY_BLINK_RENDERER_MODULES_ISTHARI_NDI_MANAGER
