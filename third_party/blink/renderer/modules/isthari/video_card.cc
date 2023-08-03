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
      main_task_runner_(base::ThreadTaskRunnerHandle::Get())
{  
    decklinkInputStream_ = nullptr;
    decklinkOutputStream_ = nullptr;

    // TODO GC
    // 1024 muetras 2 bytes 16 canales
    audioDataOut_ = (uint8_t*) malloc (1024*2*16);

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
    deckLinkAttributes->GetInt(BMDDeckLinkMaximumAudioChannels, &audioChannels_);

    deckLinkAttributes->GetInt(BMDDeckLinkDuplex, &duplexMode_);
    switch(duplexMode_) {
        case bmdDuplexInactive: VLOG(0) << "Duplex mode bmdDuplexInactive"; break;
        case bmdDuplexFull: VLOG(0) << "Duplex mode bmdDuplexFull"; break;
        case bmdDuplexSimplex: VLOG(0) << "Duplex mode bmdDuplexSimplex"; break;
        case bmdDuplexHalf: VLOG(0) << "Duplex mode bmdDuplexHalf"; break;
    }    

    deckLinkAttributes->Release();

    this->checkIO();
    this->getDisplayModes();
}

String VideoCard::identifier() { 
    std::stringstream ss;
    ss << std::hex << persistentId_;
    return String(ss.str()); 
}	

long VideoCard::duplexMode() {    
    switch(duplexMode_) {
        case bmdDuplexInactive: return 0;
        case bmdDuplexFull: return 1;
        case bmdDuplexSimplex: return 2;
        case bmdDuplexHalf: return 3;
    }    
    return -1;
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
        // hacer un disable por si ha recargado la pagina
        VLOG(0) << "Disable video output IGNORE";
/*        deckLinkOutput_->DisableVideoOutput();
        deckLinkOutput_->DisableAudioOutput();*/
        isOutput_ = true;
    }

    // check input support
    HRESULT iResult = deckLink_->QueryInterface(IID_IDeckLinkInput, (void**) &deckLinkInput_);
    if (iResult != S_OK) {
        VLOG(0) << "Card " << modelName_ << " does not support capture";
	    isInput_ = false;
    } else {
        // hacer un disable por si ha recargado la pagina
        VLOG(0) << "Disable video input IGNORE";
/*        deckLinkInput_->StopStreams();
        deckLinkInput_->DisableVideoInput();
        deckLinkInput_->DisableAudioInput();*/
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

int64_t VideoCard::audioChannels() {
    return audioChannels_;
}

VideoCardMode* VideoCard::getMode(long index) 
{
	auto l_front = modes_.begin();
	std::advance(l_front, index);
	return *l_front;
}

void VideoCard::enableVideoOutput(long mode, long audioChannels) {
    // TODO check si se puede habilitar 
    // TODO si ya lo estaba
    IDeckLinkDisplayMode *displayMode = displayModes_[(int)mode];
    this->audioChannelsOut_=audioChannels;
    this->decklinkOutputStream_ = MakeGarbageCollected<DecklinkOutputStream>(deckLinkOutput_, displayMode, audioChannels);    
}

void VideoCard::disableVideoInput() 
{   
    // TODO 
    // deckLinkInput_->DisableVideoInput();
    // deckLinkInput_->DisableAudioInput();    
}

void VideoCard::disableVideoOutput() {    
    // TODO check;
    //deckLinkOutput_->DisableVideoOutput();
    //deckLinkOutput_->DisableAudioOutput();
}

void VideoCard::enableVideoInput(ExecutionContext* executionContext, 
	long mode, 
	long selectedWidth,
	long selectedHeight,
    long channels,
 	V8VideoCardFrameCallback* frameCallback, V8VideoCardAudioCallback* audioCallback) {    

    if (decklinkInputStream_ != nullptr){        
        decklinkInputStream_->disable();
    } 
    IDeckLinkDisplayMode *displayMode = displayModes_[(int)mode];
    decklinkInputStream_ = MakeGarbageCollected<DecklinkInputStream>(
            deckLinkInput_,
            displayMode,
            channels, 
            frameCallback,
            audioCallback,
            main_task_runner_);            
}

VideoFrame* VideoCard::getVideoFrame(ExecutionContext* context) {
    return this->decklinkInputStream_->getVideoFrame(context);
}

void VideoCard::Trace(Visitor* visitor) const {
    ScriptWrappable::Trace(visitor);
    visitor->Trace(decklinkInputStream_);
    visitor->Trace(decklinkOutputStream_);
    visitor->Trace(executionContext_);
}

void VideoCard::putVideoFrame(VideoFrame* frame) {    
    decklinkOutputStream_->putVideoFrame(frame);
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
//    LOG(ERROR) << "send audio";
   // VLOG(0) << "Put audio frame "<< audioChannelsOut_;
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
    decklinkOutputStream_->sendBlackFrame();
}

} // namespace blink
