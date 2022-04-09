#include "third_party/blink/renderer/modules/isthari/audio/dom_window_audio.h"

#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "third_party/blink/renderer/modules/webcodecs/audio_data.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace blink {

#if BUILDFLAG(IS_LINUX)
pthread_t threadIsthariAudio;
void* audioData;
uint8_t **audioDataTemp;
V8VideoCardAudioCallback *isthariCallback;

void sendAudio() {
    LOG(INFO) << "send audio";   
    uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    auto time = base::Microseconds(now);
    auto frame = media::AudioBuffer::CopyFrom(media::SampleFormat::kSampleFormatS16,
    	media::ChannelLayout::CHANNEL_LAYOUT_STEREO,
    	2,
    	48000,
    	480,
    	audioDataTemp,
    	time);
    auto *frame2 = MakeGarbageCollected<AudioData>(frame);
    auto qtf = isthariCallback->handleFrame(nullptr, frame2);
    qtf.IsJust();
}


#endif

AudioManager* DOMWindowAudio::startAudioNative(LocalDOMWindow&, V8VideoCardAudioCallback* callback, ExceptionState&) {
    return MakeGarbageCollected<AudioManager>(callback); 
/*
#if BUILDFLAG(IS_LINUX)
    LOG(INFO) << "start audio native";
    isthariCallback = callback;
//    pthread_create(&threadIsthariAudio, NULL, pulse_thread, callback);
    
    auto thread = blink::Thread::CreateThread(ThreadCreationParams(ThreadType::kRealtimeAudioWorkletThread).SetSupportsGC(false));
    PostCrossThreadTask(*thread->GetTaskRunner(), FROM_HERE, CrossThreadBindOnce(&pulse_thread), WrapCrossThreadPersistent(std::move(base::ThreadTaskRunnerHandle::Get())));
#endif
*/
}

}
