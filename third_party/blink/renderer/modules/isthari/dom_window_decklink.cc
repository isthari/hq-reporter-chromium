#include "third_party/blink/renderer/modules/isthari/dom_window_decklink.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_get_callback.h"

#if BUILDFLAG(IS_LINUX)
#include "third_party/decklink/linux/DeckLinkAPI.h"
#include "third_party/decklink/linux/platform.h"
#elif BUILDFLAG(IS_WIN)
#include "third_party/decklink/win/DeckLinkAPI.h"
#include "third_party/decklink/win/platform.h"
#endif

#include "ndi/ndi_manager.h"
#include "ndi/Processing.NDI.Lib.h"

namespace blink {

NdiManager* DOMWindowDecklink::ndiManager(LocalDOMWindow& window) {
    VLOG(0) << "TEST INITIALIZE NDI";
    int status = NDIlib_initialize();
    VLOG(0) << "test initialize ndi " << status;
    return NdiManager::getInstance();
}

long DOMWindowDecklink::videoCardList(LocalDOMWindow& window, V8VideoCardGetCallback* callback, ExceptionState &exception_state) {

	#if BUILDFLAG(IS_WIN)
	// Initialize COM on this thread
	HRESULT  result1 = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(result1))
	{
		VLOG(0) << "Initialization of COM failed - result = " << result1;
		return 1;
	}
	#endif

	IDeckLinkIterator *iterator;
        HRESULT result = GetDeckLinkIterator(&iterator);
        if (result != S_OK){
                VLOG(0) << "Error creating decklink iterator";
		return 1 ;
        }
        VLOG(0) << "GetDeckLinkIterator ";

        int counter=0;
        IDeckLink *deckLink;
        while (iterator->Next(&deckLink) == S_OK) {
            VLOG(0) << "DeckLink card " << counter++;
			VideoCard *card = MakeGarbageCollected<VideoCard>(deckLink); 
			auto qtf = callback->handleCard(nullptr, card);
			if (qtf.IsJust()) {
	            VLOG(0) << "isjust";
			}
        }

        iterator->Release();
	return 1;
}

}
