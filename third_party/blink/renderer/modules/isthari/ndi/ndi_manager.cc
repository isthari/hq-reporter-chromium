#include "base/logging.h"
#include "base/timer/timer.h"
#include <string>

#include "third_party/blink/renderer/platform/heap/cross_thread_persistent.h"
#include "ndi_manager.h"

namespace blink {

NdiManager::NdiManager(){

    scanedStreams_ = std::make_shared<std::map<std::string, std::string>>();    
    VLOG(0) << "ndi manager constructor";
    this->find_ = NDIlib_find_create_v2();
    // TODO recuperar
    //this->scanTimer_.Start(FROM_HERE, base::Seconds(1), WrapCrossThreadWeakPersistent(this), &NdiManager::scanCallback);
}	

NdiManager* NdiManager::getInstance() {
    VLOG(0) << "ndi factory initialize";
    if(instance_ == nullptr) {
        VLOG(0) << "NDI factory first instance";
        NdiManager::instance_ = MakeGarbageCollected<NdiManager>();
    }	
    VLOG(0) << "NDI factory ends";
    return instance_;
}

void NdiManager::Trace(Visitor* visitor) const {
    ScriptWrappable::Trace(visitor);
}

void NdiManager::scanCallback(void) {
    uint32_t numSources = 0;
    
    if (NDIlib_find_wait_for_sources(this->find_, 1) ) {
        const NDIlib_source_t* sources = nullptr;
	    sources = NDIlib_find_get_current_sources(this->find_, &numSources);
        VLOG(0) << "new NDI detected num sources " << numSources;
        
        std::shared_ptr<std::map<std::string, std::string>> tempMap = std::make_shared<std::map<std::string, std::string>>();
        for(uint32_t i=0; i<numSources; i++) {
            NDIlib_source_t temp = sources[i];
            std::string name = temp.p_ndi_name;
            std::string url = temp.p_url_address;
            VLOG(0) << "NDI detected " << name << " url " << url;
            tempMap->insert(std::pair<std::string, std::string>(name, name));
        }
        this->scanedStreams_ = tempMap;
    }
}

void NdiManager::getScanedStreams(V8NdiScanCallback* callback) {
    std::shared_ptr<std::map<std::string, std::string>> temp = this->scanedStreams_;
    
    std::map<std::string, std::string>::iterator it = temp->begin();
    while (it != temp->end()){
        std::string url = it->first;
        std::string name = it->second;
        it++;
        VLOG(0) << "get url " << url << " name " << name;
        auto qtf = callback->onStream(nullptr, String(url), String(name));
        if (qtf.IsJust()) {
            VLOG(0) << "isjust";
        }
    }
}

NdiInputStream* NdiManager::startInputStream(String url, V8VideoCardFrameCallback* callback, V8VideoCardAudioCallback* audioCallback) {
    std::string urlS = url.Utf8();
    return MakeGarbageCollected<NdiInputStream>(urlS, callback, audioCallback);
}

NdiOutputStream* NdiManager::startOutputStream(String label) {
    std::string labelS = label.Utf8();
    return MakeGarbageCollected<NdiOutputStream>(labelS);
}

}
