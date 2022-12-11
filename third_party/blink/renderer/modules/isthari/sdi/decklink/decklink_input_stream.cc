#include "decklink_input_stream.h"
#include "../../base/callback_helper.h"

#include <chrono>
#include <stdlib.h>

#include "media/base/audio_buffer.h"
#include "third_party/blink/renderer/bindings/core/v8/generated_code_helper.h"
#include "third_party/blink/renderer/platform/heap/cross_thread_persistent.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/libyuv/include/libyuv.h"

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

    // TODO GC borrar esto?
    audioBuffer_ = (uint8_t **) malloc(sizeof (uint8_t *));
    audioBuffer_[0] = (uint8_t *) malloc (48000 * 2 * 16); // equivalente a 1 segundo 16 canales

    // Audio de entrada
    inputStart_ = 0;
    audioDataIndex_ = 0;
    audioDataCurrent_ = NULL;
    audioDataNext_ = NULL;
    // TODO GC
    audioDataTemp_ = (uint8_t **) malloc(sizeof (uint8_t *) );
    audioDataTemp_[0] = (uint8_t*) malloc(48000 * 2 * 2);

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

    // fusionar con el otro
    if (inputStart_ == 0){
        inputStart_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }     
    /*if (now==inputStart_) {
        now = now + 10000;
    }*/
    timeIn_ = base::Microseconds(now-inputStart_); 

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
        this->directAudioData(audioData);
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

    PostDelayedCrossThreadTask(*main_task_runner_, 
        FROM_HERE, 
        CrossThreadBindOnce(&DecklinkInputStream::onVideoFrameReceived, WrapCrossThreadWeakPersistent(this)), 
        base::Microseconds(0));            
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

void DecklinkInputStream::processAudioData(IDeckLinkAudioInputPacket* audioFrame){        
    uint8_t** copyBuffer;
    copyBuffer = (uint8_t **) malloc(sizeof (uint8_t *) );
    copyBuffer[0] = (uint8_t*) malloc(48000 * 2 * 2);  // equivalente a 1 segundo de audio       
      
    void *frameBytes;
    // Por redondeo pueden no tener todos los paquetes el mismo numero de samples
    int samples = (int) audioFrame->GetSampleFrameCount();
    int size = (int) (samples * 2 * 2);
    audioFrame->GetBytes(&frameBytes);
    memcpy(copyBuffer[0], frameBytes, size);
#ifdef DEBUG_AUDIO0
    fwrite(copyBuffer[0], 1, size, fptrOriginal);
#endif   			
      
    if (audioDataCurrent_ == NULL) {        
        audioDataCurrent_ = copyBuffer;
        audioDataCurrent_[0] = copyBuffer[0];
        audioSamplesCurrent_ = samples;
        timeInCurrent_ = timeIn_;        
        return;
    } else if (audioDataNext_ == NULL){        
        audioDataNext_ = copyBuffer;
        audioDataNext_[0] = copyBuffer[0];
        audioSamplesNext_ = samples;
        timeInNext_ = timeIn_;
        return;
    } 
  
    VLOG(0) << "TimeIn " << timeIn_ << " samples " << samples;
    this->inputAudioCycle();        
  
    // coger samples-audioDataIndex_
    // este no lleva delay porque esta ya pasado    
    int sent = audioSamplesCurrent_-audioDataIndex_;
    uint8_t* audioPointer;
    audioPointer = audioDataCurrent_[0];
    audioPointer += (audioDataIndex_*2*2);
    memcpy(audioDataTemp_[0], audioPointer, sent*2*2);
    audioDataIndex_ = 0;  

    int pending = 480 - sent;
    audioPointer = audioDataTemp_[0];
    audioPointer += (sent*2*2);
    memcpy(audioPointer, audioDataNext_[0], pending*2*2);
    audioDataIndex_ += pending;   
#ifdef DEBUG_AUDIO0
    fwrite(audioDataTemp_[0], 1, 480*2*2, fptr10ms);
#endif
  
    // generar el audioData y enviarlo  
    //base::TimeDelta delay = base::Microseconds(audioDataIndex_*1000/48);
    base::TimeDelta delay = base::Microseconds(0);
    VLOG(0) << "GENERAL Time in current " << timeInCurrent_ << " delay " << delay << " timestamp " << (timeInCurrent_+delay);
//  LOG(INFO) << "DELAY "<<delay;
    timeInCurrent_ = base::Microseconds(0);
    auto frame = media::AudioBuffer::CopyFrom(media::SampleFormat::kSampleFormatS16,
        media::ChannelLayout::CHANNEL_LAYOUT_STEREO,
	    2, // channel count
        48000, // sample rate
	    480, 
	    audioDataTemp_,
	    timeInCurrent_);
        /*
    PostDelayedCrossThreadTask(*main_task_runner_, 
        FROM_HERE, 
        CrossThreadBindOnce(&DecklinkInputStream::OnAudioFrameReceived, WrapCrossThreadWeakPersistent(this), frame), 
        delay);*/
    
    // rotar el audio
    free(audioDataCurrent_[0]);
    free(audioDataCurrent_);  
    audioDataCurrent_ = audioDataNext_;
    audioDataCurrent_[0] = audioDataNext_[0];
    audioSamplesCurrent_ = audioSamplesNext_;
    audioSamplesNext_ = samples;
    audioDataNext_ = NULL;
    timeInCurrent_ = timeInNext_;
  
    audioDataNext_ = copyBuffer;
    audioDataNext_[0] = copyBuffer[0];
    timeInNext_ = timeIn_;    
}

void DecklinkInputStream::inputAudioCycle() {
    // Coger al audio del buffer hasta que no queden suficientes
    while ( (audioSamplesCurrent_-audioDataIndex_) >= 480 ){
        // TODO controlar el numero de canales que llegan
        memcpy(audioDataTemp_[0], audioDataCurrent_[0]+(audioDataIndex_*2*2), 480*2*2);    
#ifdef DEBUG_AUDIO0
        fwrite(audioDataTemp_[0], 1, 480*2*2, fptr10ms);
#endif

        // generar el audioData y enviarlo
        base::TimeDelta delay = base::Microseconds(audioDataIndex_*1000/48);
        audioDataIndex_ += 480;
//    LOG(INFO) << "delay " << delay " index " << audioDataIndex;
        VLOG(0) << "AUDIO-CYCLE Time in current " << timeInCurrent_ << " delay " << delay << " timestamp " << (timeInCurrent_+delay);
        auto frame = media::AudioBuffer::CopyFrom(media::SampleFormat::kSampleFormatS16,
            media::ChannelLayout::CHANNEL_LAYOUT_STEREO,
	        2, // channel count
            48000, // sample rate
	        480, 
	        audioDataTemp_,
	        timeInCurrent_ + delay);			
        //PostDelayedCrossThreadTask(*main_task_runner_, FROM_HERE, WTF::CrossThreadBindOnce(&DecklinkInputStream::OnAudioFrameReceived,WrapCrossThreadWeakPersistent(this), frame), delay);
    }
}

void DecklinkInputStream::OnAudioFrameReceived(scoped_refptr<media::AudioBuffer> audioBuffer) {
    if (frameCounter_ < 10){
        return;
    }
    LOG(INFO) << "timestamp " << audioBuffer->timestamp();
    auto *frame2 = MakeGarbageCollected<AudioData>(audioBuffer);
    auto qtf = audioCallback_->handleFrame(nullptr, frame2);
    qtf.IsJust(); 
}

void DecklinkInputStream::directAudioData(IDeckLinkAudioInputPacket* audioFrame){ 
    void *frameBytes;
    int samples = (int) audioFrame->GetSampleFrameCount();
    int size = (int) (samples * 2 * 2);
    audioFrame->GetBytes(&frameBytes);
    memcpy(audioBuffer_[0], frameBytes, size);
    audioBufferMedia_ = media::AudioBuffer::CopyFrom(media::SampleFormat::kSampleFormatS16,    
        media::ChannelLayout::CHANNEL_LAYOUT_STEREO,
        2, // channel count
        48000, // sample rate
        samples, 
        audioBuffer_,
        currentFrameTime_);    
        
    PostCrossThreadTask(*main_task_runner_, 
        FROM_HERE, 
        CrossThreadBindOnce(&DecklinkInputStream::directAudioCallback, WrapCrossThreadWeakPersistent(this)));        
}

void DecklinkInputStream::directAudioCallback() {    
    //LOG(INFO) << "timestamp " << audioBufferMedia_->timestamp();
    auto *frame2 = MakeGarbageCollected<AudioData>(audioBufferMedia_);
    auto qtf = audioCallback_->handleFrame(nullptr, frame2);
    qtf.IsJust(); 
}

void DecklinkInputStream::onAudioDataReceived(int samples) {
    //VLOG(0) << "onAudioDataReceived";
#ifdef DEBUG_AUDIO    
    fwrite(audioBuffer_[0], 1, samples*2*2, fptrOriginal);
#endif    
    /*
    ScriptState* callback_relevant_script_state = audioCallback_->
    CallbackRelevantScriptStateOrThrowException("VideoCardAudioCallback", "handleFrame");

    if (!IsCallbackFunctionRunnable(callback_relevant_script_state,
                                  audioCallback_->IncumbentScriptState())) {
        VLOG(0) << "callback is no longer available audio sdi";
        this->disable();
    */
    /*    
    if (!isAvailableAudioDataCallback(audioCallback_)) {
        VLOG(0) << "callback is no longer available";          
    } else { 
        */
        /*
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
        */
    //}
}

}