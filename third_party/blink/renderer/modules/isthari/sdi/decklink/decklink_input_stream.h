#pragma once

#include "../../video_card_mode.h"
#include <list>
#include <map>
#include <string>

#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_audio_callback.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_frame_callback.h"
#include "third_party/blink/renderer/modules/isthari/linked_list.h"
#include "third_party/blink/renderer/modules/webcodecs/audio_data.h"
#include "third_party/blink/renderer/modules/webcodecs/video_frame.h"

#if BUILDFLAG(IS_LINUX)
#include "third_party/decklink/linux/DeckLinkAPI.h"
#elif BUILDFLAG(IS_WIN)
#include "third_party/decklink/win/DeckLinkAPI.h"
#endif

//#define DEBUG_AUDIO

namespace blink {
    class DecklinkInputStream : public ScriptWrappable,
        IDeckLinkInputCallback
    {
        DEFINE_WRAPPERTYPEINFO();
        public: 
            DecklinkInputStream(IDeckLinkInput *deckLinkInput,
                IDeckLinkDisplayMode* displayMode, 
                long channels,
                V8VideoCardFrameCallback* frameCallback,
                V8VideoCardAudioCallback* audioCallback,
                scoped_refptr<base::SingleThreadTaskRunner> main_task_runner);
            void Trace(Visitor*) const override;
            void disable();

            // IDeckLinkInputCallback
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) override { return E_NOINTERFACE; }
            ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
            ULONG STDMETHODCALLTYPE  Release(void) override { return 0; }
            HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode*, BMDDetectedVideoInputFormatFlags) override;
            HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame*, IDeckLinkAudioInputPacket*) override;

            VideoFrame* getVideoFrame(ExecutionContext* context);

        private: 
            // seccion de video
            void processVideoFrame(IDeckLinkVideoInputFrame* );            
            void onVideoFrameReceived();            

            // seccion de audio
            void processAudioData(IDeckLinkAudioInputPacket* );                        
            void processAudioBuffer(uint8_t*, int, int);
            void OnAudioFrameReceived(scoped_refptr<media::AudioBuffer>, int);            

        private:
            IDeckLinkInput *deckLinkInput_;
            IDeckLinkDisplayMode* displayMode_;
            Member<V8VideoCardFrameCallback> frameCallback_;
            Member<V8VideoCardAudioCallback> audioCallback_;
            scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;            

            // video data
            scoped_refptr<media::VideoFrame> videoFrame_;
            Member<VideoFrame> videoFrameBlink_;
            int width_;
            int height_;
            uint8_t* sourceY_;
            uint8_t* sourceU_;
            uint8_t* sourceV_;

            // imagen scalada para reducir uso de cpu
            uint8_t* scaledY_;
            uint8_t* scaledU_;
            uint8_t* scaledV_;
            int scaledWidth_;
            int scaledHeight_;
        
            // counters and timers
            uint64_t startTimestamp_;
            int32_t frameCounter_;
            int32_t frameCounterAudio_;
            base::TimeDelta currentFrameTime_;

            // COMUN                   
            // Delta de tiempo del frame actual
            base::TimeDelta timeIn_;
            uint64_t inputStart_;

            // audio data
            int channels_;
            base::TimeDelta timeInCurrent_;
            uint8_t*** audioDataTemp_;

            // nueva parte de audio
            LinkedList *rootAudio_;

//#define DEBUG_AUDIO audio
#ifdef DEBUG_AUDIO
            FILE *fptrOriginal;            
#endif                

    };
}