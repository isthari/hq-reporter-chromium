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
      main_task_runner_(base::ThreadTaskRunnerHandle::Get())
{
    frameData0 = (uint8_t*) malloc(1920*1080*4);
    frameData1 = (uint8_t*) malloc(1920*1080*4);
    buffer0 = DOMArrayBuffer::Create((const void*)frameData0, 1920*1080*4);
    buffer1 = DOMArrayBuffer::Create((const void*)frameData0, 1920*1080*4);    
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

void VideoCard::disableVideoInput() 
{
    // TODO guardar si esta activo o no para poder hacer un disable al recargar la web
    deckLinkInput_->DisableVideoInput();
    deckLinkInput_->DisableAudioInput();
}

void VideoCard::enableVideoInput(ExecutionContext* executionContext, long mode, V8VideoCardFrameCallback* frameCallback, V8VideoCardAudioCallback* audioCallback) {    
    frameCallback_ = frameCallback;
    audioCallback_ = audioCallback;
    HRESULT result;
    VLOG(0) << "Enabling Video Input ";
    IDeckLinkDisplayMode *displayMode = displayModes_[(int)mode];
//    IDeckLinkDisplayMode *displayMode = displayModes_[11];
    result = deckLinkInput_->EnableVideoInput(displayMode->GetDisplayMode(), bmdFormat8BitYUV, 0);
    if (result != S_OK) {
        VLOG(0) << "  Error enabling Video Input ";
    }

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
    visitor->Trace(buffer0);
    visitor->Trace(buffer1);
    visitor->Trace(videoFrame);
    visitor->Trace(audioCallback_);
    visitor->Trace(frameCallback_);
    visitor->Trace(executionContext_);
}

} // namespace blink
