#include "video_card.h"
#include <string>

#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/blink/renderer/modules/webcodecs/video_frame.h"
#include "third_party/decklink/linux/platform.h"

namespace blink {

VideoCard::VideoCard(IDeckLink *deckLink)
    : deckLink_(deckLink),
      main_task_runner_(base::ThreadTaskRunnerHandle::Get())
{

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
            modes_.push_back(name);
	}
    }
}

void VideoCard::enableVideoInput(V8VideoCardFrameCallback* frameCallback) {
    frameCallback_ = frameCallback;
    HRESULT result;
    VLOG(0) << "Enabling Video Input ";
    IDeckLinkDisplayMode *displayMode = displayModes_[8];
    result = deckLinkInput_->EnableVideoInput(displayMode->GetDisplayMode(), bmdFormat8BitYUV, 0);
    if (result != S_OK) {
        VLOG(0) << "  Error enabling Video Input ";
    }
/*
    HRESULT audio = deckLinkInput_->EnableAudioInput(bmdAudioSampleRate48kHz, bmdAudioSampleType16bitInteger, channels);
	if (audio == S_OK) {
		LOG_INFO("enableVideoInput AudioEnable");
//		auto thread = new std::thread([this] {
//			this->audioInputProcessor();
//		});
	} else {
		LOG_INFO("[DeckLinkCard]->enableVideoInput AudioEnable ERROR");
	}
*/
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
    VLOG(0) << "Video input frame arrived";

    // TODO generar un video_frame y devolverlo a traves del callback
    PostCrossThreadTask(*main_task_runner_, FROM_HERE,
      CrossThreadBindOnce(&VideoCard::OnVideoFrameReceived,WrapCrossThreadWeakPersistent(this)));
    return S_OK;
}
void VideoCard::OnVideoFrameReceived() {
    auto qtf = frameCallback_->handleFrame(nullptr, 10);
    qtf.IsJust();
}

void VideoCard::Trace(Visitor* visitor) const {
    ScriptWrappable::Trace(visitor);
    visitor->Trace(frameCallback_);
}

} // namespace blink
