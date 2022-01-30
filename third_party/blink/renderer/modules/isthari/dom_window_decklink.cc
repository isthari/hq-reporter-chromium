#include "third_party/blink/renderer/modules/isthari/dom_window_decklink.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_get_callback.h"
#include "third_party/decklink/linux/DeckLinkAPI.h"
#include "third_party/decklink/linux/platform.h"

namespace blink {

long DOMWindowDecklink::videoCardList(LocalDOMWindow& window, V8VideoCardGetCallback* callback, ExceptionState &exception_state) {
	VLOG(0)<< "omg 11";

	IDeckLinkIterator *iterator;
        HRESULT result = GetDeckLinkIterator(&iterator);
        if (result != S_OK){
                VLOG(0) << "Error creating decklink iterator";
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
