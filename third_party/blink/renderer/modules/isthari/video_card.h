#pragma once

#include "video_card_mode.h"
#include "sdi/decklink/decklink_input_stream.h"
#include "sdi/decklink/decklink_output_stream.h"

#include <list>
#include <map>
#include <string>

//#include "third_party/ffmpeg/libswresample/swresample.h"
#include "base/threading/thread_task_runner_handle.h"
#include "media/base/audio_buffer.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_array_buffer.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_frame_callback.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_audio_callback.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/blink/renderer/modules/webcodecs/video_frame.h"
#if BUILDFLAG(IS_LINUX)
#include "third_party/decklink/linux/DeckLinkAPI.h"
#elif BUILDFLAG(IS_WIN)
#include "third_party/decklink/win/DeckLinkAPI.h"
#endif

//#define DEBUG_AUDIO0
#ifdef DEBUG_AUDIO0
#include <stdio.h>
#endif

namespace base {
class SingleThreadTaskRunner;
}

namespace blink {

class VideoCard : public ScriptWrappable
{		  
    DEFINE_WRAPPERTYPEINFO();
public:
    VideoCard(IDeckLink* deckLink_);
    void Trace(Visitor*) const override;

    // atributos solo lectura
    // TODO falta duplex
    // TODO falta un uniqueId en formato string para que sea comun entre todos los tipos de dispositivos
    bool input() { return isInput_; }
    bool output() { return isOutput_; }
    String modelName() { return String(modelName_); }
    int64_t persistentId() { return persistentId_; }
    int64_t subDeviceIndex() { return (long) subDeviceIndex_; }
    String identifier();
    
    // Metadata
    int64_t audioChannels();
    long getModeCount() { return (long) modes_.size(); }
    VideoCardMode* getMode(long index); 

    // Input
    void enableVideoInput(ExecutionContext*, 
    	long mode,
    	long selectedWidth, long selectedHeight, 
        long channels,
    	V8VideoCardFrameCallback *, V8VideoCardAudioCallback *);
    void disableVideoInput();
    VideoFrame* getVideoFrame(ExecutionContext* context);


    // output    
    void enableVideoOutput(long mode, long audioChannels);
    void disableVideoOutput();
    void putVideoFrame(VideoFrame* frame);        
    void putAudioFrame(NotShared<DOMFloat32Array> audio0, NotShared<DOMFloat32Array> audio1,
    	NotShared<DOMFloat32Array> audio2, NotShared<DOMFloat32Array> audio3,
    	NotShared<DOMFloat32Array> audio4, NotShared<DOMFloat32Array> audio5,
    	NotShared<DOMFloat32Array> audio6, NotShared<DOMFloat32Array> audio7,
    	NotShared<DOMFloat32Array> audio8, NotShared<DOMFloat32Array> audio9,
    	NotShared<DOMFloat32Array> audio10, NotShared<DOMFloat32Array> audio11,
    	NotShared<DOMFloat32Array> audio12, NotShared<DOMFloat32Array> audio13,
    	NotShared<DOMFloat32Array> audio14, NotShared<DOMFloat32Array> audio15);
    void sendBlackFrame();

private:
    // acceso a la tarjeta
    IDeckLink* deckLink_;
    
    // nuevo modulo de entrada
    Member<DecklinkInputStream> decklinkInputStream_;
    Member<DecklinkOutputStream> decklinkOutputStream_;

    IDeckLinkOutput *deckLinkOutput_;
    IDeckLinkInput *deckLinkInput_;        

    std::map<int, IDeckLinkDisplayMode*> displayModes_;
    std::list<VideoCardMode *> modes_;

    // atributos solo lectura
    bool isInput_;
    bool isOutput_;
    std::string modelName_;
    int64_t persistentId_;
    int64_t subDeviceIndex_;
    int64_t audioChannels_;
    
    // no se usa de momento
    BMDTimeValue frameDuration_;
    BMDTimeScale frameTimescale_;

    // entrada
    Member<ExecutionContext> executionContext_;
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;

    // DECKLINK        
    // Parte de salida SDI
    long audioChannelsOut_;  
    uint8_t *audioDataOut_;    
    
private:
    void checkIO();
    void getDisplayModes();

    // callbacks para ejecutar en el main thread
    void OnVideoFrameReceived();
    void OnAudioFrameReceived(scoped_refptr<media::AudioBuffer> audioBuffer);
};

} // namespace blink
