#include "audio_manager.h"

#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "third_party/blink/renderer/modules/webcodecs/audio_data.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

#if BUILDFLAG(IS_LINUX)
#include <pthread.h>
#include <pulse/simple.h>
#include <chrono>
#elif BUILDFLAG(IS_WIN)
#endif

namespace blink {

AudioManager::AudioManager(V8VideoCardAudioCallback* callback) :
  main_task_runner_(base::ThreadTaskRunnerHandle::Get())
{
    this->start_ = 0;
    this->samples_ = 480;
    this->counter_ = 0;
    this->callback_ = callback;
    this->thread_ = blink::Thread::CreateThread(ThreadCreationParams(ThreadType::kRealtimeAudioWorkletThread).SetSupportsGC(false));
    PostCrossThreadTask(*thread_->GetTaskRunner(), FROM_HERE, CrossThreadBindOnce(&AudioManager::PulseThread, WrapCrossThreadWeakPersistent(this)));
}

void AudioManager::Trace(Visitor* visitor) const {
    ScriptWrappable::Trace(visitor);
    visitor->Trace(callback_);
}

void AudioManager::PulseThread() {
    LOG(INFO) << "pulse thread";
    
    int channels = 2;
    pa_simple* s;
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.channels = channels;
    ss.rate = 48000;

    pa_channel_map *map = new pa_channel_map();
    map = pa_channel_map_init_auto(map, channels, PA_CHANNEL_MAP_ALSA);
    map->channels = channels;

    s = pa_simple_new(NULL,               // Use the default server.
        "ISTHARI streaming",           // Our application's name.
	PA_STREAM_RECORD,
	NULL,               // Use the default device.
	"encoder",            // Description of our stream.
	&ss,                // Our sample format.
	map,               // Use default channel map
	NULL,               // Use default buffering attributes.
	NULL               // Ignore error code.
    );
    int audioSize = 2 * channels * samples_;
    audioData_ = malloc(audioSize);    
    audioDataTemp_ = (uint8_t **) malloc(sizeof (uint8_t *) );
    //audioDataTemp_[0] = (uint8_t*) malloc(2*channels*480);
    audioDataTemp_[0] = (uint8_t*) audioData_;
    int error=0;
    int result=0;
//    this->start_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()-1000000;
//    uint64_t then = 0;
    while (error==0 && result==0) {        
	result = pa_simple_read(s, audioData_, audioSize, &error);
//	uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
	
//	auto delay = base::Microseconds ( now - (start_+counter_*10000));
	auto timestamp = base::Microseconds (counter_*10000);
//	LOG(INFO) << now << "\t" << (now-then) << "\t" << delay << "\t" << timestamp << "\t" << counter_;
//	then = now;
	counter_++;
	auto frame = media::AudioBuffer::CopyFrom(media::SampleFormat::kSampleFormatS16,
	    media::ChannelLayout::CHANNEL_LAYOUT_STEREO,
	    2,
	    48000,
	    samples_,
	    audioDataTemp_,
	    timestamp);
	PostCrossThreadTask(*main_task_runner_, 
		FROM_HERE, 
		CrossThreadBindOnce(&AudioManager::SendAudio, WrapCrossThreadWeakPersistent(this), frame));
    }    
}

void AudioManager::SendAudio(scoped_refptr<media::AudioBuffer> audioBuffer) {
/*
    uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    LOG(INFO) << now << "\t" << (now-start_);
    */
    auto *frame2 = MakeGarbageCollected<AudioData>(audioBuffer);
    auto qtf = callback_->handleFrame(nullptr, frame2);
    qtf.IsJust();
}

}
