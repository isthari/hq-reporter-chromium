#include "callback_helper.h"

#include "third_party/blink/renderer/bindings/core/v8/generated_code_helper.h"

namespace blink {

bool isAvailableVideoFrameCallback(V8VideoCardFrameCallback* callback) {
    ScriptState* callback_relevant_script_state = 
        callback->CallbackRelevantScriptStateOrThrowException("VideoCardFrameCallback", "handleFrame");    
    return IsCallbackFunctionRunnable(callback_relevant_script_state, callback->IncumbentScriptState());
}

bool isAvailableAudioDataCallback(V8VideoCardAudioCallback* callback) {
    // ni idea de porque aqui no funciona pero directamente en el stream si
    ScriptState* callback_relevant_script_state = callback-> CallbackRelevantScriptStateOrThrowException("VideoCardAudioCallback", "handleFrame");

    return !IsCallbackFunctionRunnable(callback_relevant_script_state, callback->IncumbentScriptState());
}

}