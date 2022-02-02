#include "video_card.h"
#include <chrono>
#include <string>

#include "media/base/audio_buffer.h"
#include "media/base/video_frame.h"

#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/blink/renderer/modules/webcodecs/audio_data.h"
#include "third_party/blink/renderer/modules/webcodecs/video_frame.h"
#include "third_party/decklink/linux/platform.h"
#include "third_party/libyuv/include/libyuv.h"

namespace blink {

VideoCard::VideoCard(IDeckLink *deckLink)
    : deckLink_(deckLink),
      isInputEnabled_(false),
      isOutputEnabled_(false),
      inputVideoMode_(-1),
      outputVideoMode_(-1),
      main_task_runner_(base::ThreadTaskRunnerHandle::Get())
{
    frameData0 = (uint8_t*) malloc(1920*1080*4);
    frameData1 = (uint8_t*) malloc(1920*1080*4);
    frameCounter_ = 0;

    audioData0 = (uint8_t **) malloc(sizeof (uint8_t *) );
    audioData1 = (uint8_t **) malloc(sizeof (uint8_t *) );    
    audioData0[0] = (uint8_t*) malloc(48000 * 2 * 2);  // equivalente a 1 segundo de audio    audi
    audioData1[0] = (uint8_t*) malloc(48000 * 2 * 2);  // equivalente a 1 segundo de audio    audi    
    audioCounter_ = 0;
    audioStart_ = 0;

    dlstring_t deviceNameString;
    deckLink_->GetModelName(&deviceNameString);
    modelName_ = DlToStdString(deviceNameString);
    DeleteString(deviceNameString);	
    VLOG(0) << "Detected card " << modelName_;
    
    VLOG(0) << "GetAttributes";
    IDeckLinkProfileAttributes *deckLinkAttributes = NULL;
    deckLink->QueryInterface(IID_IDeckLinkProfileAttributes,
			(void**) &deckLinkAttributes);

    deckLinkAttributes->GetInt(BMDDeckLinkPersistentID, &persistentId_);
    deckLinkAttributes->GetInt(BMDDeckLinkSubDeviceIndex, &subDeviceIndex_);
    deckLinkAttributes->Release();

    this->checkIO();
    this->getDisplayModes();
}	

/**
 * Comprueba si soporta entrada / salida
 */
void VideoCard::checkIO() {
    // check output support
    HRESULT oResult = this->deckLink_->QueryInterface(IID_IDeckLinkOutput, (void**) &this->deckLinkOutput_);
    if (oResult != S_OK) {
        VLOG(0) << "Card " << modelName_ << " does not support playback ";
	isOutput_ = false;
    } else {
        isOutput_ = true;
    }

    // check input support
    HRESULT iResult = deckLink_->QueryInterface(IID_IDeckLinkInput, (void**) &deckLinkInput_);
    if (iResult != S_OK) {
        VLOG(0) << "Card " << modelName_ << " does not support capture";
	isInput_ = false;
    } else {
        isInput_ = true;	   
	deckLinkInput_->SetCallback(this);
    }
}

void VideoCard::getDisplayModes() {
    VLOG(0) << "[DeckLinkCard]->getDisplayModes()";
    IDeckLinkDisplayModeIterator *displayModeIterator;
    IDeckLinkDisplayMode *displayMode;
    if (deckLinkInput_ != NULL){
        deckLinkInput_->GetDisplayModeIterator(&displayModeIterator);
    } else{
        deckLinkOutput_->GetDisplayModeIterator(&displayModeIterator);
    }

    if (deckLinkInput_==NULL && deckLinkOutput_==NULL){
        VLOG(0) << "No decklink input/output";
    } else {
        int index = 0;
	while (displayModeIterator->Next(&displayMode) == S_OK) {
            dlstring_t displayModeName;
	    displayMode->GetName(&displayModeName);
	    std::string name= DlToStdString(displayModeName);
	    DeleteString(displayModeName);
	    VLOG(0) << "      " << index << " DisplayModeName " << name;
      	    displayModes_[index] = displayMode;
            index++;

            VideoCardMode* mode = MakeGarbageCollected<VideoCardMode>(name, 
			    displayMode->GetWidth(), 
			    displayMode->GetHeight(),
			    0.0);
            modes_.push_back(mode);	    
	    // TODO crear un campo en la clase para guardar el modo seleccionado
	}
    }
}

VideoCardMode* VideoCard::getMode(long index) 
{
	auto l_front = modes_.begin();
	std::advance(l_front, index);
	return *l_front;
}

void VideoCard::enableVideoOutput(long mode) {
    if (!isOutput_) {
        VLOG(0) << "Device does not support video output";
        return;
    }
    
    if (isOutputEnabled_) {
        VLOG(0) << "Output already enabled";
        return;
    }
    
    // TODO verificar si admite full duplex antes de desactivar
    if (isInputEnabled_ ) {
        VLOG(0) << "Disable video input";
        this->disableVideoInput();
    }
    
    isOutputEnabled_ = true;
    outputVideoMode_ = mode;
    IDeckLinkDisplayMode *displayMode = displayModes_[(int)mode];
    BMDDisplayMode bmdMode = displayMode->GetDisplayMode();
    HRESULT video = deckLinkOutput_->EnableVideoOutput(bmdMode, bmdVideoOutputRP188);
    if (video == S_OK) {
        VLOG(0) << "enable video output ok";
    } else {
        VLOG(0) << "enable video output error";
    }
    
    // TODO GC
    deckLinkOutput_->CreateVideoFrame((int32_t) displayMode->GetWidth(),
		    	(int32_t) displayMode->GetHeight(),
		    	(int32_t) displayMode->GetWidth()*2, // asume siempre 8 bit YUV
		    	bmdFormat8BitYUV,
		    	bmdFrameFlagDefault,
		    	&playbackFrame_);
		    	
    HRESULT audio = deckLinkOutput_->EnableAudioOutput(bmdAudioSampleRate48kHz, bmdAudioSampleType16bitInteger, 2, bmdAudioOutputStreamContinuous);
    if (audio == S_OK) {
        VLOG(0) << "enable audio output ok";        
    } else {
        VLOG(0) << "enable audio output error";
    }
}

void VideoCard::disableVideoInput() 
{
    // TODO guardar si esta activo o no para poder hacer un disable al recargar la web
    if (isInputEnabled_) {
        deckLinkInput_->DisableVideoInput();
        deckLinkInput_->DisableAudioInput();
    }
    isInputEnabled_ = false;
}

void VideoCard::disableVideoOutput() {
    if (isOutputEnabled_) {
        deckLinkOutput_->DisableVideoOutput();
        deckLinkOutput_->DisableAudioOutput();
    }
    isOutputEnabled_ = false;
}

void VideoCard::enableVideoInput(ExecutionContext* executionContext, long mode, V8VideoCardFrameCallback* frameCallback, V8VideoCardAudioCallback* audioCallback) {    
    if (isInputEnabled_) {
        VLOG(0) << "Input already enabled";
        return;
    }
    
    inputVideoMode_ = mode;
    
    // TODO desactivar si esta en modo output y no soporta full duplex

    frameCallback_ = frameCallback;
    audioCallback_ = audioCallback;
    HRESULT result;
    
    // Bloque de video
    VLOG(0) << "Enabling Video Input ";
    IDeckLinkDisplayMode *displayMode = displayModes_[(int)mode];
    result = deckLinkInput_->EnableVideoInput(displayMode->GetDisplayMode(), bmdFormat8BitYUV, 0);
    if (result != S_OK) {
        VLOG(0) << "  Error enabling Video Input ";
    }

    // bloque de audio
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
        isInputEnabled_ = true;
    }
}

HRESULT VideoCard::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode*, BMDDetectedVideoInputFormatFlags) {
    // TODO verificar el formato de entrada
    VLOG(0) << "Video input format changed";
    return S_OK;
}

HRESULT VideoCard::VideoInputFrameArrived(
		IDeckLinkVideoInputFrame *videoFrame,
		IDeckLinkAudioInputPacket *audioFrame) {    
    if(videoFrame) {
        if (videoFrame->GetFlags() & bmdFrameHasNoInputSource) {
	    // no data
	} else {
	    // TODO llevarlo a una funcion solo video
            // VLOG(0) << "Video input frame arrived";
            // TODO generar un video_frame y devolverlo a traves del callback
	    
	    void *frameBytes;
	    auto result = videoFrame->GetBytes(&frameBytes);
	    //VLOG(0) << "pre convert "<<result << " " << videoFrame->GetWidth() << " "<<videoFrame->GetHeight()<< " "<< videoFrame->GetPixelFormat()<< " "<<videoFrame->GetFlags();
	    if (result == S_OK){
	        libyuv::UYVYToARGB((const uint8_t*) frameBytes, 1920*2,
		   	    (frameCounter_%2==0)?frameData0:frameData1,
			    1920*4,
			    1920, 1080);
		//VLOG(0) << "post convert";			    
		PostCrossThreadTask(*main_task_runner_, FROM_HERE, CrossThreadBindOnce(&VideoCard::OnVideoFrameReceived,WrapCrossThreadWeakPersistent(this)));
		//VLOG(0) << "post input frame arrived";
	    }            
        }
    }

    if (audioFrame) {
	// TODO llevarlo a una funcion solo audio
	void *frameBytes;
	int samples = (int) audioFrame->GetSampleFrameCount();
	// TODO como saber cuantos canales hay?
    //    VLOG(0) << "audio frame samples " << samples;
	int size = (int) (samples * 2 * 2);
	audioFrame->GetBytes(&frameBytes);
	memcpy(audioCounter_%2==0?audioData0[0]:audioData1[0], frameBytes, size);	
        PostCrossThreadTask(*main_task_runner_, FROM_HERE, CrossThreadBindOnce(&VideoCard::OnAudioFrameReceived,WrapCrossThreadWeakPersistent(this), samples));    
    }
  
    return S_OK;
}

void VideoCard::OnVideoFrameReceived() {
    if(!frameCallback_->IsCallbackObjectCallable()) {
//        VLOG(0) << "No video callback available";     
        return;
    }

    //VLOG(0) << "pre wrap";
    //VLOG(0) << "pre handle frame";
    auto qtf = frameCallback_->handleFrame(nullptr, 10);
    //VLOG(0) << "pre isjust";
    qtf.IsJust();
    //VLOG(0) << "post ist just";
}

VideoFrame* VideoCard::getVideoFrame(ExecutionContext* context) {    
    //VLOG(0) << "getVideoFrame call";
    gfx::Size size(1920, 1080);
    base::TimeDelta timestamp;
    auto frame = media::VideoFrame::WrapExternalData(media::PIXEL_FORMAT_ARGB,
		    size,
		    gfx::Rect(size),
		    size,
		    (frameCounter_%2==0)?frameData0:frameData1, 1920*1080*4,
		    timestamp);
    frameCounter_++;	    
    //VLOG(0) << "pre create "<<frameCounter_;	   
    this->videoFrame = MakeGarbageCollected<VideoFrame>(frame, context);

    return this->videoFrame;
}

void VideoCard::OnAudioFrameReceived(int samples) {
    if(!audioCallback_->IsCallbackObjectCallable()) {
//        VLOG(0) << "No audio callback available";
        return;
    }

//    VLOG(0) << "audio frame samples: " << samples;
   
    if (audioStart_ == 0){
        audioStart_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    } 
    uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    //base::TimeDelta timestamp = base::Microseconds(0);
    auto frame = media::AudioBuffer::CopyFrom(media::SampleFormat::kSampleFormatS16,
		    media::ChannelLayout::CHANNEL_LAYOUT_STEREO,
		    2, // channel count
                    48000, // sample rate
		    samples, 
		    (const uint8_t* const*) (audioCounter_%2==0)?audioData0:audioData1,		    
		    // TODO sumar al timestamp en funcion del sample rate, cambiar int por long long
		    base::Microseconds(now-audioStart_));
		//    base::Microseconds(audioCounter_ * 1600.0/48000.0*1000000.0)); 
    //timestamp);   
    audioCounter_++;
		    
    auto *frame2 = MakeGarbageCollected<AudioData>(frame);
    auto qtf = audioCallback_->handleFrame(nullptr, frame2);
    qtf.IsJust(); 
}

void VideoCard::Trace(Visitor* visitor) const {
    ScriptWrappable::Trace(visitor);
    visitor->Trace(videoFrame);
    visitor->Trace(audioCallback_);
    visitor->Trace(frameCallback_);
    visitor->Trace(executionContext_);
}

} // namespace blink
