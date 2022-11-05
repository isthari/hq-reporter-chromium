#include "decklink_input_stream.h"
#include "../../base/callback_helper.h"

#include <chrono>
#include <stdlib.h>

#include "media/base/audio_buffer.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/libyuv/include/libyuv.h"

#include "third_party/blink/renderer/bindings/core/v8/generated_code_helper.h"


namespace blink {
    
DecklinkInputStream::DecklinkInputStream(IDeckLinkInput *decklinkInput, 
            IDeckLinkDisplayMode* displayMode, 
            V8VideoCardFrameCallback* frameCallback,
            V8VideoCardAudioCallback* audioCallback,
            scoped_refptr<base::SingleThreadTaskRunner> main_task_runner)
    :   deckLinkInput_(decklinkInput),
        displayMode_(displayMode),
        frameCallback_(frameCallback),
        audioCallback_(audioCallback),
        main_task_runner_(main_task_runner),
        scaledWidth_(1280),
        scaledHeight_(720),
        startTimestamp_(0),
        frameCounter_(0)
{
    VLOG(0) << "Enabling video input";    
    width_ = (int) displayMode_->GetWidth();
    height_ = (int) displayMode_->GetHeight();
    VLOG(0) << "video input width " << width_ << " height " << height_;

    // TODO GC    
    sourceY_ = (uint8_t*) malloc (width_*1.5*height_);
    sourceU_ = (uint8_t*) malloc (width_/2*height_);
    sourceV_ = (uint8_t*) malloc (width_/2*height_);

    // TODO GC, imagen que se usa escalada para la codificacion
    scaledY_ = (uint8_t*) malloc (scaledWidth_*1.5*scaledHeight_);
    scaledU_ = (uint8_t*) malloc (scaledWidth_/2*scaledHeight_);
    scaledV_ = (uint8_t*) malloc (scaledWidth_/2*scaledHeight_);
    /*
    inWidth_ = (int) selectedWidth;
    inHeight_ = (int) selectedHeight;
    inStY_ = (uint8_t*) malloc (inWidth_*1.5*inHeight_);
    inStU_ = (uint8_t*) malloc (inWidth_/2*inHeight_);
    inStV_ = (uint8_t*) malloc (inWidth_/2*inHeight_);
    */

    // TODO GC
    audioBuffer_ = (uint8_t **) malloc(sizeof (uint8_t *));
    audioBuffer_[0] = (uint8_t *) malloc (48000 * 2 * 16); // equivalente a 1 segundo 16 canales

    deckLinkInput_->SetCallback(this);

    // TODO separar la inicializacion del constructor para gestion de errores
    HRESULT result;
    result = deckLinkInput_->EnableVideoInput(displayMode->GetDisplayMode(), bmdFormat8BitYUV, 0);
    if (result != S_OK) {
        VLOG(0) << "  Error enabling Video Input " << result;
        if (result == S_FALSE) {
          VLOG(0) << "Error S_FALSE";
        } else if (result == E_UNEXPECTED) {
          VLOG(0) << "Error E_UNEXPECTED";
        } else if (result == E_NOTIMPL) {
          VLOG(0) << "Error E_NOTIMPL";
        } else if (result == E_OUTOFMEMORY) {
          VLOG(0) << "Error E_OUTOFMEMORY";
        } else if (result == E_INVALIDARG) {
          VLOG(0) << "Error E_INVALIDARG";
        } else if (result == E_NOINTERFACE) {
          VLOG(0) << "Error E_NOINTERFACE";
        } else if (result == E_POINTER) {
          VLOG(0) << "Error E_POINTER";
        } else if (result == E_HANDLE) {
          VLOG(0) << "Error E_HANDLE";
        } else if (result == E_ABORT) {
          VLOG(0) << "Error E_ABORT";
        } else if (result == E_FAIL) {
          VLOG(0) << "Error E_FAIL";
        } else if (result == E_ACCESSDENIED) {
          VLOG(0) << "Error E_ACCESSDENIED";
        }
    }

    // TODO añadir soporte para los 16 canales restantes		    	
    int channels = 2;
    HRESULT audio = deckLinkInput_->EnableAudioInput(bmdAudioSampleRate48kHz, bmdAudioSampleType16bitInteger, channels);
    if (audio == S_OK) {
        VLOG(0) << "Enabled audio input";
    } else {
        VLOG(0) << "AudioEnable ERROR";
    }

    result = deckLinkInput_->StartStreams();
    if (result != S_OK) {
        VLOG(0) << "  Error starting streams ";
    } else {
        VLOG(0) << "Input enabled";
        // TODO poner una marca para que nose inicialize 2 veces
    }

#ifdef DEBUG_AUDIO
    fptrOriginal = fopen("/home/jhernan/Desktop/borrable/audio_original.pcm", "w");
#endif    
}

void DecklinkInputStream::Trace(Visitor* visitor) const {
    ScriptWrappable::Trace(visitor);
    visitor->Trace(audioCallback_);
    visitor->Trace(audioData_);
    visitor->Trace(frameCallback_);        
    visitor->Trace(videoFrameBlink_);
}

void DecklinkInputStream::disable() {
    VLOG(0) << "KK Disable SDI device";
    deckLinkInput_->StopStreams();
    deckLinkInput_->DisableVideoInput();
    deckLinkInput_->DisableAudioInput();     
}

HRESULT DecklinkInputStream::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode*, BMDDetectedVideoInputFormatFlags) {
    // TODO verificar el formato de entrada
    VLOG(0) << "Video input format changed";
    return S_OK;
}

HRESULT DecklinkInputStream::VideoInputFrameArrived(
		IDeckLinkVideoInputFrame *videoFrame,
		IDeckLinkAudioInputPacket *audioData) {                   
    if (startTimestamp_ == 0){
        startTimestamp_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    } 
    		
    uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    if (now==startTimestamp_) {
        now = now + 10000;
    }
    currentFrameTime_ = base::Microseconds(now-startTimestamp_);    		

    // enviar el audio lo primero     
    /*
    if (audioFrame) {
        processInputAudio(audioFrame);
    }
    */

    if(videoFrame) {

        if (videoFrame->GetFlags() & bmdFrameHasNoInputSource) {
	        // no data
	    } else {        
            this->processVideoFrame(videoFrame);	    	    
        }
    }
    if (audioData) {
        this->processAudioData(audioData);
    }

    frameCounter_++;
    return S_OK;
}

void DecklinkInputStream::processVideoFrame(IDeckLinkVideoInputFrame *videoFrame) {    	    
	    
    void *frameBytes;
    auto result = videoFrame->GetBytes(&frameBytes);
    if (result!=S_OK){
        VLOG(0) << "Error getting video frame bytes";
        return;
    }
	
    // convertir UYVY a I420
    libyuv::UYVYToI420((const uint8_t*) frameBytes, 
            width_*2,   
	  	    sourceY_, width_*1.5,
	   	    sourceU_, width_/2,
	   	    sourceV_, width_/2,
		    width_, height_);		               

    // escalar
    libyuv::I420Scale(sourceY_, width_*1.5,
        sourceU_, width_/2,
        sourceV_, width_/2,
        width_, height_,
        scaledY_, scaledWidth_*1.5,
        scaledU_, scaledWidth_/2,
        scaledV_, scaledWidth_/2,
        scaledWidth_, scaledHeight_, 
        libyuv::FilterMode::kFilterBilinear);

    PostCrossThreadTask(*main_task_runner_, 
        FROM_HERE, 
        CrossThreadBindOnce(&DecklinkInputStream::onVideoFrameReceived,
        WrapCrossThreadWeakPersistent(this)));

    /*          
            // generar la imagen que se manda al codificador
            libyuv::I420Scale(dstY_, width*1.5,
               	dstU_, width/2,
               	dstV_, width/2,
               	width, height,
               	inStY_, inWidth_*1.5,
               	inStU_, inWidth_/2,
               	inStV_, inWidth_/2,
               	inWidth_, inHeight_, 
               	libyuv::FilterMode::kFilterBilinear);
		PostCrossThreadTask(*main_task_runner_, FROM_HERE, CrossThreadBindOnce(&VideoCard::OnVideoFrameReceived,WrapCrossThreadWeakPersistent(this)));
    */
}

void DecklinkInputStream::onVideoFrameReceived() {    
    if (!isAvailableVideoFrameCallback(frameCallback_)) {
        VLOG(0) << "Callback is no longer available video SDI";
        this->disable();
    } else {                
        auto qtf = frameCallback_->handleFrame(nullptr, frameCounter_);
        qtf.IsJust();
    }
}

VideoFrame* DecklinkInputStream::getVideoFrame(ExecutionContext* context) {    	
    /*       	
    gfx::Size size(width_, height_);        
	videoFrame_ = media::VideoFrame::WrapExternalYuvData(media::PIXEL_FORMAT_I420,
	    size,
	    gfx::Rect(size),
	    size,
	    width_*1.5,
	    width_/2,
	    width_/2,
	    sourceY_,
	    sourceU_,
	    sourceV_,
	    currentFrameTime_);    				
        */

    // enviar la escalada
    gfx::Size size(scaledWidth_, scaledHeight_);        
    videoFrame_ = media::VideoFrame::WrapExternalYuvData(media::PIXEL_FORMAT_I420,
	    size,
	    gfx::Rect(size),
	    size,
	    scaledWidth_*1.5,
	    scaledWidth_/2,
	    scaledWidth_/2,
	    scaledY_,
	    scaledU_,
	    scaledV_,
	    currentFrameTime_); 

    this->videoFrameBlink_ = MakeGarbageCollected<VideoFrame>(videoFrame_, context);
    return this->videoFrameBlink_;
}

void DecklinkInputStream::processAudioData(IDeckLinkAudioInputPacket* audioData){    
    int samples = (int) audioData->GetSampleFrameCount();
    int size = (int) (samples * 2 * 2);
    void *frameBytes;
    audioData->GetBytes(&frameBytes);
    memcpy(audioBuffer_[0], frameBytes, size);
    
    PostCrossThreadTask(*main_task_runner_, 
        FROM_HERE, 
        CrossThreadBindOnce(&DecklinkInputStream::onAudioDataReceived,
        WrapCrossThreadWeakPersistent(this),
        samples));
}

void DecklinkInputStream::onAudioDataReceived(int samples) {
    //VLOG(0) << "onAudioDataReceived";
#ifdef DEBUG_AUDIO    
    fwrite(audioBuffer_[0], 1, samples*2*2, fptrOriginal);
#endif    
    ScriptState* callback_relevant_script_state = audioCallback_->
    CallbackRelevantScriptStateOrThrowException("VideoCardAudioCallback", "handleFrame");

    if (!IsCallbackFunctionRunnable(callback_relevant_script_state,
                                  audioCallback_->IncumbentScriptState())) {
        VLOG(0) << "callback is no longer available audio sdi";
        this->disable();
    /*    
    if (!isAvailableAudioDataCallback(audioCallback_)) {
        VLOG(0) << "callback is no longer available";    
        */
    } else { 
        audioBufferMedia_ = media::AudioBuffer::CopyFrom(media::SampleFormat::kSampleFormatS16,
            media::ChannelLayout::CHANNEL_LAYOUT_STEREO,
            2, // channel count
            48000, // sample rate
            samples, 
            audioBuffer_,
            currentFrameTime_);
        
        audioData_ = MakeGarbageCollected<AudioData>(audioBufferMedia_);    
        auto qtf = audioCallback_->handleFrame(nullptr, audioData_);
        qtf.IsJust(); 
    }
}

}