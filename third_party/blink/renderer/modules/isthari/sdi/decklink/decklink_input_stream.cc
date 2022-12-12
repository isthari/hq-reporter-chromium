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

    // Audio de entrada
    // TODO GC
    audioDataTemp_ = (uint8_t **) malloc(sizeof (uint8_t *) );
    audioDataTemp_[0] = (uint8_t*) malloc(48000 * 2 * 2);

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
    currentFrameTime_ = base::Microseconds(now-startTimestamp_+40000);    		

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

    PostDelayedCrossThreadTask(*main_task_runner_, 
        FROM_HERE, 
        CrossThreadBindOnce(&DecklinkInputStream::onVideoFrameReceived,
        WrapCrossThreadWeakPersistent(this)), base::Microseconds(15000));
    /*
    PostCrossThreadTask(*main_task_runner_, 
        FROM_HERE, 
        CrossThreadBindOnce(&DecklinkInputStream::onVideoFrameReceived,
        WrapCrossThreadWeakPersistent(this)));*/

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

#define SIZE 480
void DecklinkInputStream::processAudioData(IDeckLinkAudioInputPacket* audioFrame){           
    int samples = (int) audioFrame->GetSampleFrameCount();
    int size = samples * 2* 2;

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
        audioPointer += (rootAudio_->index*2*2);           
        if ( (rootAudio_->samples - rootAudio_->index) > SIZE) {
            // hay suficiente en un frame
            finish = false;                        
            memcpy(audioDataTemp_[0], audioPointer, SIZE*2*2);
            rootAudio_->index = rootAudio_->index + SIZE;
        } else if (rootAudio_->next != NULL) {
            // hay suficiente en un frame + el siguiente
            finish = false;            
            int size = rootAudio_->samples - rootAudio_->index;            
            memcpy(audioDataTemp_[0], audioPointer, size*2*2);
            LinkedList *old = rootAudio_;
            rootAudio_ = old->next;
            free(old->audioBuffer_[0]);
            free(old->audioBuffer_);
            free(old);

            // copiar el resto
            int pending = SIZE - size;            
            memcpy(audioDataTemp_[0], audioPointer, size*2*2);  
            uint8_t* audioPointer2 = rootAudio_->audioBuffer_[0];            
            rootAudio_->index = pending;
            memcpy(audioDataTemp_[0]+(size*2*2), audioPointer2, pending*2*2);
        }

        if (!finish) {
            auto frame = media::AudioBuffer::CopyFrom(media::SampleFormat::kSampleFormatS16,
                media::ChannelLayout::CHANNEL_LAYOUT_STEREO,
	            2, // channel count
                48000, // sample rate
	            480, 
	            audioDataTemp_,                
	            timeInCurrent_);
            PostCrossThreadTask(*main_task_runner_, 
                FROM_HERE, 
                CrossThreadBindOnce(&DecklinkInputStream::OnAudioFrameReceived,WrapCrossThreadWeakPersistent(this), frame));    
        }
    }
}    

void DecklinkInputStream::OnAudioFrameReceived(scoped_refptr<media::AudioBuffer> audioBuffer) {    
    // LOG(INFO) << "timestamp " << audioBuffer->timestamp();
    auto *frame2 = MakeGarbageCollected<AudioData>(audioBuffer);
    auto qtf = audioCallback_->handleFrame(nullptr, frame2);
    qtf.IsJust(); 
}

}