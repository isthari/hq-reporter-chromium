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
      isOutputEnabled_(false),      
      outputVideoMode_(-1),
      main_task_runner_(base::ThreadTaskRunnerHandle::Get())
{  
    decklinkInputStream_ = nullptr;

    // TODO GC
    // 1024 muetras 2 bytes 16 canales
    audioDataOut_ = (uint8_t*) malloc (1024*2*16);

    framesOutVideo_ = 0;

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

            BMDTimeValue frameRateDuration;
            BMDTimeScale frameRateScale;
            displayMode->GetFrameRate(&frameRateDuration, &frameRateScale);

            VideoCardMode* mode = MakeGarbageCollected<VideoCardMode>(name, 
			    displayMode->GetWidth(), 
			    displayMode->GetHeight(),
			    (long) frameRateDuration,
			    (long) frameRateScale,
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

void VideoCard::enableVideoOutput(long mode, long audioChannels) {
    if (!isOutput_) {
        VLOG(0) << "Device does not support video output";
        return;
    }
    
    if (isOutputEnabled_) {
        VLOG(0) << "Output already enabled";
        return;
    }
            
    isOutputEnabled_ = true;
    outputVideoMode_ = mode;
    audioChannelsOut_ = audioChannels;
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
    HRESULT audio = deckLinkOutput_->EnableAudioOutput(bmdAudioSampleRate48kHz, bmdAudioSampleType16bitInteger, (uint32_t) audioChannels, bmdAudioOutputStreamContinuous);
    if (audio == S_OK) {
        VLOG(0) << "enable audio output ok";        
    } else {
        VLOG(0) << "enable audio output error";
    }
}

void VideoCard::disableVideoInput() 
{   
    // TODO 
    // deckLinkInput_->DisableVideoInput();
    // deckLinkInput_->DisableAudioInput();    
}

void VideoCard::disableVideoOutput() {    
    // TODO
    deckLinkOutput_->DisableVideoOutput();
    deckLinkOutput_->DisableAudioOutput();
}

void VideoCard::enableVideoInput(ExecutionContext* executionContext, 
	long mode, 
	long selectedWidth,
	long selectedHeight,
 	V8VideoCardFrameCallback* frameCallback, V8VideoCardAudioCallback* audioCallback) {    

    if (this->decklinkInputStream_ == nullptr){        
        IDeckLinkDisplayMode *displayMode = displayModes_[(int)mode];
        decklinkInputStream_ = MakeGarbageCollected<DecklinkInputStream>(
                deckLinkInput_,
                displayMode, 
                frameCallback,
                audioCallback,
                main_task_runner_);        
    }     
}

VideoFrame* VideoCard::getVideoFrame(ExecutionContext* context) {
    return this->decklinkInputStream_->getVideoFrame(context);
}

void VideoCard::Trace(Visitor* visitor) const {
    ScriptWrappable::Trace(visitor);
    visitor->Trace(decklinkInputStream_);
    visitor->Trace(executionContext_);
}

void VideoCard::putVideoFrame(VideoFrame* frame) {
    if (!isOutputEnabled_) {
      return;
    }
/*
    LOG(ERROR) << "format: " << std::__to_underlying(frame->format()->AsEnum())
	<< " width " << frame->codedWidth()
	<< " height " << frame->codedHeight();
	*/
    auto width = frame->codedWidth();
    auto height = frame->codedHeight();
    gfx::Size size(height, width);
    auto mediaFrame = frame->frame();
    uint8_t *deckLinkBuffer = nullptr;
    
    /*
    if (mediaFrame->format()==media::VideoPixelFormat::PIXEL_FORMAT_NV12) {
        int strideY = mediaFrame->stride(0);
        int strideUV = mediaFrame->stride(1);
        LOG(ERROR) << "strideY "<< strideY << " strideUV " << strideUV;
    }    
    
    LOG(ERROR) << "width " << width << " height " << height;
    std::vector<int> strides = media::VideoFrame::ComputeStrides(media::VideoPixelFormat::PIXEL_FORMAT_NV12, size);
    for(int n : strides) {
        LOG(ERROR) << "stride "<<n;
    }*/
    
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

int float2int(float f, int index, uint8_t *audioDataOut_) {
  uint8_t a1;
  uint8_t a2;
  int out = f * 32768;
  a1 = (uint8_t) (out >> 8 & 0xff);
  a2 = (uint8_t) (out & 0xff);
  audioDataOut_[index++] = a2;
  audioDataOut_[index++] = a1;
  return index;
}

void VideoCard::putAudioFrame(NotShared<DOMFloat32Array> audio0, NotShared<DOMFloat32Array> audio1,
    	NotShared<DOMFloat32Array> audio2, NotShared<DOMFloat32Array> audio3,
    	NotShared<DOMFloat32Array> audio4, NotShared<DOMFloat32Array> audio5,
    	NotShared<DOMFloat32Array> audio6, NotShared<DOMFloat32Array> audio7,
    	NotShared<DOMFloat32Array> audio8, NotShared<DOMFloat32Array> audio9,
    	NotShared<DOMFloat32Array> audio10, NotShared<DOMFloat32Array> audio11,
    	NotShared<DOMFloat32Array> audio12, NotShared<DOMFloat32Array> audio13,
    	NotShared<DOMFloat32Array> audio14, NotShared<DOMFloat32Array> audio15) { 
  if (!isOutputEnabled_) {
    return;
  }
//    LOG(ERROR) << "send audio";

    int index = 0;
    DOMFloat32Array* a0 = audio0.Get();
    DOMFloat32Array* a1 = audio1.Get();
    DOMFloat32Array* a2 = audio2.Get();
    DOMFloat32Array* a3 = audio3.Get();    
    DOMFloat32Array* a4 = audio4.Get();    
    DOMFloat32Array* a5 = audio5.Get();    
    DOMFloat32Array* a6 = audio6.Get();    
    DOMFloat32Array* a7 = audio7.Get();    
    DOMFloat32Array* a8 = audio8.Get();    
    DOMFloat32Array* a9 = audio9.Get();    
    DOMFloat32Array* a10 = audio10.Get();    
    DOMFloat32Array* a11 = audio11.Get();    
    DOMFloat32Array* a12 = audio12.Get();    
    DOMFloat32Array* a13 = audio13.Get();        
    DOMFloat32Array* a14 = audio14.Get();
    DOMFloat32Array* a15 = audio15.Get();    
    
    const float* aa0 = (const float*) a0->buffer()->Data();
    const float* aa1 = (const float*) a1->buffer()->Data();
    const float* aa2 = (const float*) a2->buffer()->Data();
    const float* aa3 = (const float*) a3->buffer()->Data();
    const float* aa4 = (const float*) a4->buffer()->Data();
    const float* aa5 = (const float*) a5->buffer()->Data();
    const float* aa6 = (const float*) a6->buffer()->Data();
    const float* aa7 = (const float*) a7->buffer()->Data();
    const float* aa8 = (const float*) a8->buffer()->Data();
    const float* aa9 = (const float*) a9->buffer()->Data();
    const float* aa10 = (const float*) a10->buffer()->Data();
    const float* aa11 = (const float*) a11->buffer()->Data();
    const float* aa12 = (const float*) a12->buffer()->Data();    
    const float* aa13 = (const float*) a13->buffer()->Data();    
    const float* aa14 = (const float*) a14->buffer()->Data();    
    const float* aa15 = (const float*) a15->buffer()->Data();            
    
    for (int i=0; i<480; i++) {
      index = float2int(aa0[i], index, audioDataOut_);
      index = float2int(aa1[i], index, audioDataOut_);
        
      if (audioChannelsOut_ >= 8) {
        index = float2int(aa2[i], index, audioDataOut_);
        index = float2int(aa3[i], index, audioDataOut_);
        index = float2int(aa4[i], index, audioDataOut_);
        index = float2int(aa5[i], index, audioDataOut_);
        index = float2int(aa6[i], index, audioDataOut_);
        index = float2int(aa7[i], index, audioDataOut_);
      }
      
      if (audioChannelsOut_==16) {
        index = float2int(aa8[i], index, audioDataOut_);
        index = float2int(aa9[i], index, audioDataOut_);
        index = float2int(aa10[i], index, audioDataOut_);
        index = float2int(aa11[i], index, audioDataOut_);
        index = float2int(aa12[i], index, audioDataOut_);
        index = float2int(aa13[i], index, audioDataOut_);
        index = float2int(aa14[i], index, audioDataOut_);
        index = float2int(aa15[i], index, audioDataOut_);
      }
    }
    uint32_t written;    
    deckLinkOutput_->WriteAudioSamplesSync((void*) audioDataOut_, 480, &written);
}

void VideoCard::sendBlackFrame() {
IDeckLinkDisplayMode *displayMode = displayModes_[(int)outputVideoMode_];
    uint32_t width = (uint32_t) displayMode->GetWidth();
    uint32_t height = (uint32_t) displayMode->GetHeight();   
    int dstStrideY = width*1.5;
    int dstStrideU = width/4;
    int dstStrideV = width/4;
    memset(dstY_, 0, dstStrideY * height);
    memset(dstU_, 128, dstStrideU * height);
    memset(dstV_, 128, dstStrideV * height);        

    uint8_t *deckLinkBuffer = nullptr;
    playbackFrame_->GetBytes((void**) &deckLinkBuffer);        
    libyuv::I420ToUYVY(dstY_, dstStrideY,
    			dstU_, dstStrideU,
    			dstV_, dstStrideV,
			deckLinkBuffer,
			width*2, width, height-1);
    deckLinkOutput_->DisplayVideoFrameSync(playbackFrame_);    
}

} // namespace blink
