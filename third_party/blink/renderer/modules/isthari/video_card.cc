#include "video_card.h"
#include <chrono>
#include <string>
#include <sstream>

#include "media/base/audio_buffer.h"
#include "media/base/video_frame.h"

#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/blink/renderer/modules/webcodecs/audio_data.h"
#include "third_party/blink/renderer/modules/webcodecs/video_frame.h"
#if BUILDFLAG(IS_LINUX)
#include "third_party/decklink/linux/platform.h"
#elif BUILDFLAG(IS_WIN)
#include "third_party/decklink/win/platform.h"
#endif
#include "third_party/libyuv/include/libyuv.h"

#include <utility>

namespace blink {

VideoCard::VideoCard(IDeckLink *deckLink)
    : deckLink_(deckLink),
      isInputEnabled_(false),
      isOutputEnabled_(false),
      inputVideoMode_(-1),
      outputVideoMode_(-1),
      main_task_runner_(base::ThreadTaskRunnerHandle::Get())
{  
    // TODO GC
    audioDataOut_ = (uint8_t*) malloc (1024*2*2);

    framesOutVideo_ = 0;
    // TODO GC
    frameData = (uint8_t*) malloc(1920*1080*4);
    audioData = (uint8_t **) malloc(sizeof (uint8_t *) );
    audioData[0] = (uint8_t*) malloc(48000 * 2 * 2);  // equivalente a 1 segundo de audio 
    frameInCounter_ = 0;
    inputStart_ = 0;

    dlstring_t deviceNameString;
    deckLink_->GetModelName(&deviceNameString);
    modelName_ = DlToStdString(deviceNameString);
    DeleteString(deviceNameString);	
    VLOG(0) << "Detected card " << modelName_;
    
    VLOG(0) << "GetAttributes";
    IDeckLinkProfileAttributes *deckLinkAttributes = NULL;
    deckLink->QueryInterface(IID_IDeckLinkProfileAttributes, (void**) &deckLinkAttributes);

    deckLinkAttributes->GetInt(BMDDeckLinkPersistentID, &persistentId_);
    deckLinkAttributes->GetInt(BMDDeckLinkSubDeviceIndex, &subDeviceIndex_);
    deckLinkAttributes->Release();

    this->checkIO();
    this->getDisplayModes();

    // crear el resampler
    // TODO GC
/*    this->resampler_ = swr_alloc_set_opts(NULL,
		    AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, 48000,
		    AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP, 48000,
		    0, NULL);
		    */
}

String VideoCard::identifier() { 
    std::stringstream ss;
    ss << std::hex << persistentId_;
    return String(ss.str()); 
}	

/**
 * Comprueba si soporta entrada / salida
 */
void VideoCard::checkIO() {
    // TODO añadir duplex
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
    
    // TODO GC
    long width = displayMode->GetWidth();
    long height = displayMode->GetHeight();
    dstY_ = (uint8_t*) malloc (width*1.5*height);
    dstU_ = (uint8_t*) malloc (width/4*height);
    dstV_ = (uint8_t*) malloc (width/4*height);
    
    displayMode->GetFrameRate(&frameDuration_, &frameTimescale_);
    BMDDisplayMode bmdMode = displayMode->GetDisplayMode();            
    HRESULT video = deckLinkOutput_->EnableVideoOutput(bmdMode, bmdVideoOutputRP188);
    if (video == S_OK) {
        VLOG(0) << "enable video output ok";
    } else {
        VLOG(0) << "enable video output error";
    }
    
    // TODO GC
    VLOG(0) << "create video frame " << width << "x" << height;
    deckLinkOutput_->CreateVideoFrame((int32_t) width,
		    	(int32_t) height,
		    	(int32_t) width*2, // asume siempre 8 bit YUV
		    	bmdFormat8BitYUV,
		    	bmdFrameFlagDefault,
		    	&playbackFrame_);
		    	
    // TODO añadir soporte para los 16 canales restantes		    	
    HRESULT audio = deckLinkOutput_->EnableAudioOutput(bmdAudioSampleRate48kHz, bmdAudioSampleType16bitInteger, 2, bmdAudioOutputStreamContinuous);
    if (audio == S_OK) {
        VLOG(0) << "enable audio output ok";        
    } else {
        VLOG(0) << "enable audio output error";
    }
}

void VideoCard::disableVideoInput() 
{
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
    if (inputStart_ == 0){
        inputStart_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    } 
    		
    uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    if (now==inputStart_) {
        now = now + 10000;
    }
    timeIn_ = base::Microseconds(now-inputStart_);    		
    if(videoFrame) {
        if (videoFrame->GetFlags() & bmdFrameHasNoInputSource) {
	    // no data
	} else {
	    // TODO llevarlo a una funcion solo video                        	    
	    IDeckLinkDisplayMode *displayMode = displayModes_[(int)inputVideoMode_];
	    int width = (int) displayMode->GetWidth();
	    int height = (int) displayMode->GetHeight();
	    
	    void *frameBytes;
	    auto result = videoFrame->GetBytes(&frameBytes);
	    if (result == S_OK){
	        libyuv::UYVYToARGB((const uint8_t*) frameBytes, width*2,		   	    
		   	    frameData,
			    width*4,
			    width, height);
		PostCrossThreadTask(*main_task_runner_, FROM_HERE, CrossThreadBindOnce(&VideoCard::OnVideoFrameReceived,WrapCrossThreadWeakPersistent(this)));
	    }            
        }
    }

    if (audioFrame) {
	// TODO llevarlo a una funcion solo audio
	void *frameBytes;
	int samples = (int) audioFrame->GetSampleFrameCount();
	int size = (int) (samples * 2 * 2);
	audioFrame->GetBytes(&frameBytes);
	memcpy(audioData[0], frameBytes, size);			
        PostCrossThreadTask(*main_task_runner_, FROM_HERE, CrossThreadBindOnce(&VideoCard::OnAudioFrameReceived,WrapCrossThreadWeakPersistent(this), samples));    
    }
    frameInCounter_++;

    return S_OK;
}

void VideoCard::OnVideoFrameReceived() {
    if(!frameCallback_->IsCallbackObjectCallable()) {
//        VLOG(0) << "No video callback available";     
        return;
    }

    auto qtf = frameCallback_->handleFrame(nullptr, frameInCounter_);
    qtf.IsJust();
}

VideoFrame* VideoCard::getVideoFrame(ExecutionContext* context) {    
    IDeckLinkDisplayMode *displayMode = displayModes_[(int)inputVideoMode_];
    int width = (int) displayMode->GetWidth();
    int height = (int) displayMode->GetHeight();    
    gfx::Size size(width, height);

    auto frame = media::VideoFrame::WrapExternalData(media::PIXEL_FORMAT_ARGB,
		    size,
		    gfx::Rect(size),
		    size,
		    frameData,
		    width*height*4,
		    timeIn_);    	
    this->videoFrame = MakeGarbageCollected<VideoFrame>(frame, context);
    return this->videoFrame;
}

void VideoCard::OnAudioFrameReceived(int samples) {
    if(!audioCallback_->IsCallbackObjectCallable()) {
//        VLOG(0) << "No audio callback available";
        return;
    }

    auto frame = media::AudioBuffer::CopyFrom(media::SampleFormat::kSampleFormatS16,
		    media::ChannelLayout::CHANNEL_LAYOUT_STEREO,
		    2, // channel count
                    48000, // sample rate
		    samples, 
		    audioData,
		    timeIn_);
		    
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

void VideoCard::putVideoFrame(VideoFrame* frame) {
/*
    LOG(ERROR) << "format: " << std::__to_underlying(frame->format()->AsEnum())
	<< " width " << frame->codedWidth()
	<< " height " << frame->codedHeight();
	*/

    auto width = frame->codedWidth();
    auto height = frame->codedHeight();
    auto mediaFrame = frame->frame();

    uint8_t *deckLinkBuffer = nullptr;
    
    IDeckLinkDisplayMode *displayMode = displayModes_[(int)outputVideoMode_];
    uint32_t widthOut = (uint32_t) displayMode->GetWidth();
    uint32_t heightOut = (uint32_t) displayMode->GetHeight();    
    int dstStrideY = widthOut;
    int dstStrideU = widthOut/2;
    int dstStrideV = widthOut/2;
    
    playbackFrame_->GetBytes((void**) &deckLinkBuffer);
    if (width==widthOut && height==heightOut) {
        // si coinciden los parametros		
        libyuv::I420ToUYVY(mediaFrame->data(0), mediaFrame->stride(0),
			mediaFrame->data(1), mediaFrame->stride(1),
			mediaFrame->data(2), mediaFrame->stride(2),
			deckLinkBuffer,
			width*2, width, height-1);			
    } else {
        // scale
        libyuv::I420Scale(mediaFrame->data(0), mediaFrame->stride(0),
		mediaFrame->data(1), mediaFrame->stride(1),
		mediaFrame->data(2), mediaFrame->stride(2),
		width, height,
		dstY_, dstStrideY, 		
		dstU_, dstStrideU,
		dstV_, dstStrideV,
		widthOut, heightOut,
		libyuv::FilterMode::kFilterBox);
    
        libyuv::I420ToUYVY(dstY_, dstStrideY,
    			dstU_, dstStrideU,
    			dstV_, dstStrideV,
			deckLinkBuffer,
			widthOut*2, widthOut, heightOut-1);
    }			
    deckLinkOutput_->DisplayVideoFrameSync(playbackFrame_);
}

void VideoCard::putAudioFrame(NotShared<DOMFloat32Array> audioL, NotShared<DOMFloat32Array> audioR) { 
//    LOG(ERROR) << "send audio";
    int index = 0;
    DOMFloat32Array* aL0 = audioL.Get();
    DOMFloat32Array* aR0 = audioR.Get();
    const float* aL1 = (const float*) aL0->buffer()->Data();
    const float* aR1 = (const float*) aR0->buffer()->Data();
    uint8_t a1;
    uint8_t a2;
    int out;
    for (int i=0; i<480; i++) {
	out = aL1[i] * 32768;
	a1 = (uint8_t) (out >> 8 & 0xff);
	a2 = (uint8_t) (out & 0xff);
	audioDataOut_[index++] = a2;
	audioDataOut_[index++] = a1;

	out = aR1[i] * 32768;
        a1 = (uint8_t) (out >> 8 & 0xff);
        a2 = (uint8_t) (out & 0xff);
	audioDataOut_[index++] = a2;
	audioDataOut_[index++] = a1;
    }
    uint32_t written;    
    deckLinkOutput_->WriteAudioSamplesSync((void*) audioDataOut_, 480, &written);
}

} // namespace blink
