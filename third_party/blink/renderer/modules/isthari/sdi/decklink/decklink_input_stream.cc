#include "decklink_input_stream.h"
#include "../../base/callback_helper.h"

#include <chrono>
#include <stdlib.h>

#include "media/base/audio_buffer.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/blink/renderer/platform/heap/cross_thread_persistent.h"
#include "third_party/libyuv/include/libyuv.h"

#include "third_party/blink/renderer/bindings/core/v8/generated_code_helper.h"


namespace blink {
    
DecklinkInputStream::DecklinkInputStream(IDeckLinkInput *decklinkInput, 
            IDeckLinkDisplayMode* displayMode, 
            long channels,
            V8VideoCardFrameCallback* frameCallback,
            V8VideoCardAudioCallback* audioCallback,
            scoped_refptr<base::SingleThreadTaskRunner> main_task_runner)
    :   deckLinkInput_(decklinkInput),
        displayMode_(displayMode),
        frameCallback_(std::move(frameCallback)),
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

    // Audio de entrada
    // TODO GC
    channels_ = (int) channels;
    frameCounterAudio_ = 0;
    audioDataTemp_ = (uint8_t ***) malloc(sizeof (uint8_t *) * channels / 2);    
    for (int i=0; i<channels/2; i++){
        audioDataTemp_[i] = (uint8_t **) malloc(sizeof (uint8_t *));    
        audioDataTemp_[i][0] = (uint8_t*) malloc(48000 * 2 * 2);
    }    
    
    // TODO GC
    // nueva parte de audio
    rootAudio_ = NULL;

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
        return;
    }    

    HRESULT audio = deckLinkInput_->EnableAudioInput(bmdAudioSampleRate48kHz, bmdAudioSampleType16bitInteger, (uint32_t) channels_);
    if (audio == S_OK) {
        VLOG(0) << "Enabled audio input";
    } else {
        VLOG(0) << "AudioEnable ERROR";
        return;
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
    VLOG(0) << "Frame arrived";
    if (startTimestamp_ == 0){
        startTimestamp_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    } 
    		
    uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    if (now==startTimestamp_) {
        now = now + 10000;
    }
    currentFrameTime_ = base::Microseconds(now-startTimestamp_+40000);    		

    // fusionar con el otro
    if (inputStart_ == 0){
        inputStart_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }         
    timeIn_ = base::Microseconds(now-inputStart_); 
  
    if(videoFrame) {
        if (videoFrame->GetFlags() & bmdFrameHasNoInputSource) {
	        // no data
	    } else {        
            this->processVideoFrame(videoFrame);	    	    
        }
    }
    /*
    if (audioData) {
        this->processAudioData(audioData);
    }
*/
    frameCounter_++;
    return S_OK;
}

void DecklinkInputStream::processVideoFrame(IDeckLinkVideoInputFrame *videoFrame) {    	      
    VLOG(0) << "processVideoFrame";
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

    VLOG(0) << "onVideoFrameReceived pre";
    PostCrossThreadTask(*main_task_runner_, 
        FROM_HERE, 
        CrossThreadBindOnce(&DecklinkInputStream::onVideoFrameReceived,
        WrapCrossThreadWeakPersistent(this)));
}

void DecklinkInputStream::onVideoFrameReceived() {    
    VLOG(0) << "onVideoFrameReceived";
    /*
    if (!isAvailableVideoFrameCallback(frameCallback_)) {
        VLOG(0) << "alpha";
        VLOG(0) << "Callback is no longer available video SDI";
        this->disable();
    } else {                
        VLOG(0) << "beta";
        auto qtf = frameCallback_->handleFrame(nullptr, frameCounter_);
        qtf.IsJust();
    }*/
    VLOG(0) << "tetha";
    if (frameCallback_->IsCallbackObjectCallable()) {
        VLOG(0) << "true";
    } else {
        VLOG(0) << "false";
    }
    auto qtf = frameCallback_->handleFrame(nullptr, frameCounter_);
    VLOG(0) << "tetha2";
    qtf.IsJust();
    VLOG(0) << "gamma";
}

VideoFrame* DecklinkInputStream::getVideoFrame(ExecutionContext* context) {    	
    VLOG(0) << "getVideoFrame";
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

#define SIZE 480
void DecklinkInputStream::processAudioData(IDeckLinkAudioInputPacket* audioFrame){           
    int samples = (int) audioFrame->GetSampleFrameCount();
    int size = samples * channels_ * 2;

    // insertar en la cola
    LinkedList *node = new LinkedList();
    node->index = 0;
    node->samples = samples;
    node->audioBuffer_ = (uint8_t**) malloc(sizeof(uint8_t*));
    node->audioBuffer_[0] = (uint8_t*) malloc(size);
    node->timestamp = timeIn_;
    node->next = NULL;
    void *frameBytes;    
    audioFrame->GetBytes(&frameBytes);
    memcpy(node->audioBuffer_[0], frameBytes, size);     
    
    if (rootAudio_ == NULL) {     
        rootAudio_ = node;
    } else {
        rootAudio_->next = node;
    } 

    // procesar la cola
    bool finish = false;
    while (!finish) {
        finish = true;     
        uint8_t* audioPointer;
        audioPointer = rootAudio_->audioBuffer_[0];        
        audioPointer += (rootAudio_->index * channels_ * 2);            
        if (rootAudio_ == NULL) {
            // el ultimo ha limpiado el buffer no seguir
            return;
        } else if ( (rootAudio_->samples - rootAudio_->index) >= SIZE) {            
            
            // hay suficiente en un frame
            finish = false;                        
            rootAudio_->index = rootAudio_->index + SIZE;
            processAudioBuffer(audioPointer, SIZE, 0);            
        } else if (rootAudio_->next != NULL) {
            finish = false;            
            int sizeOld = rootAudio_->samples - rootAudio_->index;
            processAudioBuffer(audioPointer, sizeOld, 0);
            rootAudio_->index = rootAudio_->samples;

            LinkedList* nextRootAudio = rootAudio_->next;
            audioPointer = nextRootAudio->audioBuffer_[0];
            int sizeNew = SIZE - sizeOld;
            processAudioBuffer(audioPointer, sizeNew, sizeOld);
            //VLOG(0) << "next size " << nextRootAudio->samples;
            nextRootAudio->index = nextRootAudio->index + sizeNew;
        } else {
            //VLOG(0) << "no hay next";
            return;
        }
 
        if (rootAudio_->index >= rootAudio_->samples){
            //VLOG(0) << "clean";
            LinkedList *old = rootAudio_;
            rootAudio_ = old->next;
            free(old->audioBuffer_[0]);
            free(old->audioBuffer_);
            free(old);                        
        }
      
        frameCounterAudio_++;
        /*
        for (int channel=0; channel<channels_/2; channel++){
            auto frame = media::AudioBuffer::CopyFrom(media::SampleFormat::kSampleFormatS16,
                media::ChannelLayout::CHANNEL_LAYOUT_STEREO,
                2, // channel count
                48000, // sample rate
                SIZE, 
                audioDataTemp_[channel],                
                timeIn_);            
            PostCrossThreadTask(*main_task_runner_, 
                FROM_HERE, 
                CrossThreadBindOnce(&DecklinkInputStream::OnAudioFrameReceived,WrapCrossThreadWeakPersistent(this), std::move(frame), channel));
        }*/
    }
}    

void DecklinkInputStream::processAudioBuffer(uint8_t* audioPointer, int size, int offset) {
    for (int i=0; i<size; i++){                
        for (int channel=0; channel<channels_/2; channel++){
            *(audioDataTemp_[channel][0] + (offset*2*2 + i*2*2)) = audioPointer[i*channels_*2 + channel*4];
            *(audioDataTemp_[channel][0] + (offset*2*2 + i*2*2 + 1)) = audioPointer[i*channels_*2 + channel*4 + 1];
            *(audioDataTemp_[channel][0] + (offset*2*2 + i*2*2 + 2)) = audioPointer[i*channels_*2 + channel*4 + 2];
            *(audioDataTemp_[channel][0] + (offset*2*2 + i*2*2 + 3)) = audioPointer[i*channels_*2 + channel*4 + 3];
        }
    }
}

void DecklinkInputStream::OnAudioFrameReceived(scoped_refptr<media::AudioBuffer> audioBuffer, int index) {    
    // LOG(INFO) << "timestamp " << audioBuffer->timestamp();    
    auto *frame2 = MakeGarbageCollected<AudioData>(audioBuffer);
    auto qtf = audioCallback_->handleFrame(nullptr, frame2, index);
    qtf.IsJust(); 
}

}