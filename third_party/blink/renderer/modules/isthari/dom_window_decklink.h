#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_WEBDATABASE_DOM_WINDOW_DECKLINK_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_WEBDATABASE_DOM_WINDOW_DECKLINK_H_

#include "video_card.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_get_callback.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_card_get_callback.h"

#include "ndi/ndi_manager.h"

namespace blink {

class ExceptionState;
class LocalDOMWindow;

class MODULES_EXPORT DOMWindowDecklink {
  STATIC_ONLY(DOMWindowDecklink);

public:
    static long videoCardList(LocalDOMWindow&, V8VideoCardGetCallback* callback, ExceptionState&);
    static NdiManager* ndiManager(LocalDOMWindow& );
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_WEBDATABASE_DOM_WINDOW_DECKLINK_H_

