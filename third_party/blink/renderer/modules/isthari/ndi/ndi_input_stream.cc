#include "../base/callback_helper.h"

#include "base/logging.h"
#include "base/task/thread_pool.h"
#include "gpu/command_buffer/client/gpu_memory_buffer_manager.h"
#include "media/base/audio_buffer.h"
#include "media/base/video_frame.h"
#include "media/video/gpu_video_accelerator_factories.h"
#include "ndi_input_stream.h"
#include "third_party/blink/renderer/modules/webcodecs/audio_data.h"
#include "third_party/blink/renderer/modules/webcodecs/gpu_factories_retriever.h"
#include "third_party/blink/renderer/modules/webcodecs/video_frame.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/libyuv/include/libyuv.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/convert_from_argb.h"
#include "third_party/blink/renderer/bindings/core/v8/generated_code_helper.h"

#include <chrono>

namespace blink {

/**
* Nunca se utilizan url, porque son susceptibles de cambio al reiniciar los streams
* se usa siempre el nombre de stream, que es fijo y no cambia
*/
NdiInputStream::NdiInputStream(std::string url, V8VideoCardFrameCallback* frameCallback, V8VideoCardAudioCallback* audioCallback) :
    audioCallback_(audioCallback),
    frameCallback_(frameCallback),
    scaledWidth_(1280),
    scaledHeight_(720),
    startTimestamp_(0),
    frameCounter_(0),
    url_(url),
    enabled_(true),
    gpuPoolInitialized_(false)
    //gpuPool_(nullptr)
{
    main_task_runner_ = base::ThreadTaskRunnerHandle::Get();
      
    base::TaskTraits default_traits = {};
    // no puede ser singleThreadTaskRunner porque solo ejecutaria un thread para el primer NDI que se arranca
    //this->taskRunner_ = base::ThreadPool::CreateSingleThreadTaskRunner(default_traits);
    this->taskRunner_ = base::ThreadPool::CreateTaskRunner(default_traits);
    this->taskRunner_->PostTask(FROM_HERE, base::BindOnce(&NdiInputStream::startInternal, WrapCrossThreadWeakPersistent(this)));
    
    // TODO dimensionar en funcion de la entrada real
    int width = 1920;
    int height = 1080;
    i420originalSizeY_ = (uint8_t*) malloc (width*1.5*height);
    i420originalSizeU_ = (uint8_t*) malloc (width/2*height);
    i420originalSizeV_ = (uint8_t*) malloc (width/2*height);

    // TODO GC, imagen que se usa escalada para la codificacion
    scaledY_ = (uint8_t*) malloc (scaledWidth_*1.5*scaledHeight_);
    scaledU_ = (uint8_t*) malloc (scaledWidth_/2*scaledHeight_);
    scaledV_ = (uint8_t*) malloc (scaledWidth_/2*scaledHeight_);

    // TODO GC NV12
    nv12Y_ = (uint8_t*) malloc(scaledWidth_*2*scaledHeight_);
    nv12UV_ = (uint8_t*) malloc(scaledWidth_*scaledHeight_);
    nv12Width_ = scaledWidth_;
    nv12Height_ = scaledHeight_;
    VLOG(0) << "NV12 " << nv12Width_ << "," << nv12Height_;
    
    // TODO GC
    // coger 1 segundo de 16 canales 48khz
    audioDataTemp_ = (uint8_t **) malloc(sizeof (uint8_t *) );
    audioDataTemp_[0] = (uint8_t*) malloc(48000 * 16 * 2);

    // los task runners deben pertener al current thread
    media_task_runner_ = base::ThreadPool::CreateSingleThreadTaskRunner(default_traits);
    copy_task_runner_ = base::ThreadPool::CreateTaskRunner(default_traits);
    RetrieveGpuFactoriesWithKnownEncoderSupport(CrossThreadBindOnce(&NdiInputStream::retrievedGpuVideoAcceleratorFactories, WrapCrossThreadWeakPersistent(this)));
}

void NdiInputStream::retrievedGpuVideoAcceleratorFactories(media::GpuVideoAcceleratorFactories* factories) {
    
    VLOG(0) << "retrieved GPU video accelerator factories";    
    gpuPool_ = std::make_unique<media::GpuMemoryBufferVideoFramePool>(media_task_runner_, copy_task_runner_, factories);
    VLOG(0) << "GpuMemoryBufferVideoFramePool";
    //gfx::Size size(1920, 1080);        
    /*
    gpu::GpuMemoryBufferManager* gpuMemoryBufferManager = factories->GpuMemoryBufferManager();
    gpuMemoryBuffer_ = gpuMemoryBufferManager->CreateGpuMemoryBuffer(size, 
        gfx::BufferFormat::YUV_420_BIPLANAR,
        gfx::BufferUsage::GPU_READ,
        gpu::kNullSurfaceHandle, 
        nullptr);    */
    gpuPoolInitialized_ = true;    
}

void NdiInputStream::Trace(Visitor* visitor) const {
    ScriptWrappable::Trace(visitor);
    visitor->Trace(audioCallback_);
    visitor->Trace(frameCallback_);
    visitor->Trace(videoFrame);
}	

void NdiInputStream::disable() {
    this->enabled_ = false;
}

void NdiInputStream::startInternal() {    
    VLOG(0) << "NdiInputStream startInternal " << url_;
    
    NDIlib_source_t source;
    source.p_ndi_name = url_.c_str();
    //source.p_url_address = url_.c_str();
	
    NDIlib_recv_create_v3_t NDI_recv_create_desc;
    NDI_recv_create_desc.source_to_connect_to = source;
    NDI_recv_create_desc.p_ndi_recv_name = url_.c_str();

    NDIlib_recv_instance_t receiver = NDIlib_recv_create_v3(&NDI_recv_create_desc);
    VLOG(0) << "NDI created receiver " << url_;
    
    while(enabled_) {
        NDIlib_video_frame_v2_t videoFrame;
	    NDIlib_audio_frame_v2_t audioFrame;
	    NDIlib_metadata_frame_t metadata_frame;
	
        /// calculo de tiempos
        auto status = NDIlib_recv_capture_v2(receiver, &videoFrame, &audioFrame, &metadata_frame, 2000);
        if (startTimestamp_ ==0 && (status==NDIlib_frame_type_video || status==NDIlib_frame_type_audio)) {
            startTimestamp_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        }
        uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            if (now == startTimestamp_) {
                now = now + 10000;
            }
            base::TimeDelta timestamp = base::Microseconds(now-startTimestamp_); 
        
        switch (status) {
        case NDIlib_frame_type_none:
            //  creo que es una estructura y simplemente se recicla.
            // Tenerlo en ejecucion 24h para verificar
            VLOG(0) << "NDI no frame recreating " << url_;
            //receiver = NDIlib_recv_create_v3(&NDI_recv_create_desc);                
            NDIlib_recv_connect(receiver, &source);
            break;
        case NDIlib_frame_type_video:
            //VLOG(0) << "NDI received video " << url_;
            this->processVideoFrame(videoFrame, timestamp);
            NDIlib_recv_free_video_v2(receiver, &videoFrame);
            break;
        case NDIlib_frame_type_audio:    
            //VLOG(0) << "NDI received audio " << url_;	    
            this->processAudio(audioFrame, timestamp);
            NDIlib_recv_free_audio_v2(receiver, &audioFrame);
            break;
        case NDIlib_frame_type_status_change:
            VLOG(0) << "NDI status change " << url_;
            break;
        case NDIlib_frame_type_metadata:
            VLOG(0) << "NDIlib_frame_type_metadata " << url_;
            break;
        case NDIlib_frame_type_max:
            VLOG(0) << "NDIlib_frame_type_max " << url_;
            break;		
        case NDIlib_frame_type_error:
            VLOG(0) << "NDIlib_frame_type_error " << url_;
            break;		        	
        }        
    }
}

void NdiInputStream::processVideoFrame(NDIlib_video_frame_v2_t videoFrame, base::TimeDelta timestamp){
    int width = videoFrame.xres;
    int height = videoFrame.yres;
    frameCounter_++;
    currentFrameTime_ = timestamp;
    
    bool known = false;
    // TODO separar la conversion de imagenes a clase a parte
    if(videoFrame.FourCC == NDIlib_FourCC_video_type_e::NDIlib_FourCC_video_type_BGRA) {
        known = true;
        //VLOG(0) << "NDI convert BGRA " << url_;
        libyuv::ARGBToI420((const uint8_t*) videoFrame.p_data, width*4,
            i420originalSizeY_, (int) (width*1.5),
            i420originalSizeU_, (int) width/2,
            i420originalSizeV_, (int) width/2,
            width, height);           	    	
    } else if(videoFrame.FourCC == NDIlib_FourCC_video_type_e::NDIlib_FourCC_video_type_UYVY) {
        known = true;
        libyuv::UYVYToI420((const uint8_t*) videoFrame.p_data, width*2,   
	    i420originalSizeY_, width*1.5,
	    i420originalSizeU_, width/2,
	    i420originalSizeV_, width/2,
	    width, height);
    } else {
        VLOG(0) << "NDI critical unknown FourCC ";
        debugFourCC(videoFrame);
    }
    
    if (known) {
        libyuv::I420Scale(i420originalSizeY_, width*1.5,
            i420originalSizeU_, width/2,
            i420originalSizeV_, width/2,
            width, height,
            scaledY_, scaledWidth_*1.5,
            scaledU_, scaledWidth_/2,
            scaledV_, scaledWidth_/2,
            scaledWidth_, scaledHeight_, 
            libyuv::FilterMode::kFilterBilinear);

        if (gpuPoolInitialized_) {
            // convertir a NV12


            gfx::Size size(width, height);
            //VLOG(0) << "WrapExternalYuvData";
            /*
            int I420ToNV12(const uint8_t* src_y,
               int src_stride_y,
               const uint8_t* src_u,
               int src_stride_u,
               const uint8_t* src_v,
               int src_stride_v,
               uint8_t* dst_y,
               int dst_stride_y,
               uint8_t* dst_uv,
               int dst_stride_uv,
               int width,
               int height);

            videoFrame_ = media::VideoFrame::WrapExternalYuvData(media::PIXEL_FORMAT_NV12, size, gfx::Rect(size), size,
                width*2, width, i420originalSizeY_, i420originalSizeU_, timestamp);
                */
                
            videoFrame_ = media::VideoFrame::WrapExternalYuvData(media::PIXEL_FORMAT_I420,
	            size,
	            gfx::Rect(size),
	            size,
	            width*1.5,
	            width/2,
	            width/2,
	            i420originalSizeY_,
	            i420originalSizeU_,
	            i420originalSizeV_,
	            timestamp);
            //VLOG(0) << "MaybeCreateHardwareFrame";
            gpuPool_->MaybeCreateHardwareFrame(std::move(videoFrame_), 
                base::BindOnce(&NdiInputStream::frameReadyCB, WrapCrossThreadWeakPersistent(this)));
        }                
    }
}

void NdiInputStream::frameReadyCB(scoped_refptr<media::VideoFrame> frame) {       
    //VLOG(0) << "frame ready CB";
    if (frame->HasTextures()){
        //VLOG(0) << "has textures";
    }
    videoFrame_ = std::move(frame);
    PostCrossThreadTask(*main_task_runner_, 
            FROM_HERE, 
            CrossThreadBindOnce(&NdiInputStream::OnVideoFrameReceived,
                WrapCrossThreadWeakPersistent(this)));    
    /*
    videoFrame_ = frame;
    auto qtf = frameCallback_->handleFrame(nullptr, frameCounter_);
    qtf.IsJust();
    */
}

void NdiInputStream::OnVideoFrameReceived() {
    if (!isAvailableVideoFrameCallback(frameCallback_)) {
        VLOG(0) << "Callback is no longer available NDI";
        this->disable();
    } else {
        /*
        gfx::Size size(scaledWidth_, scaledHeight_);        
        videoFrame_ = media::VideoFrame::WrapExternalYuvData(media::PIXEL_FORMAT_I420,
            size,
            gfx::Rect(size),
            size,
            scaledWidth_*1.5,
            scaledWidth_/2,
            scaledWidth_/2,
            scaledY_,
            scaledU_,
            scaledV_,
            currentFrameTime_);   
            */
        auto qtf = frameCallback_->handleFrame(nullptr, frameCounter_);
        qtf.IsJust();
    }
}

VideoFrame* NdiInputStream::getVideoFrame(ExecutionContext* context) {    
    
    //if (gpuPoolInitialized_) {
        //VLOG(0) << "pre post";
    //    gpuPool_->MaybeCreateHardwareFrame(videoFrame_, base::BindOnce(frameReadyCaB));
        //VLOG(0) << "post post";
    //}
    this->videoFrame = MakeGarbageCollected<VideoFrame>(videoFrame_, context);
    return this->videoFrame;
}

void NdiInputStream::processAudio(NDIlib_audio_frame_v2_t audioFrame, base::TimeDelta timestamp) {
    int sampleRate = audioFrame.sample_rate;
    int channels = audioFrame.no_channels;
    int samples = audioFrame.no_samples;
    int bytesPerChannel = 16;
    /*
    VLOG(0) << "NDI " << url_ << " samples " << samples << " channels " << channels  << " sample rate " << sampleRate << " bytesPerChannel " << bytesPerChannel;
    */

    NDIlib_audio_frame_interleaved_16s_t audio_frame_16bpp_interleaved;
    audio_frame_16bpp_interleaved.reference_level = 0;     // We are going to have 20dB of headroom
    audio_frame_16bpp_interleaved.p_data = new short[samples * channels];
    NDIlib_util_audio_to_interleaved_16s_v2(&audioFrame, &audio_frame_16bpp_interleaved);
   
    memcpy(audioDataTemp_[0], audio_frame_16bpp_interleaved.p_data, channels * samples * (bytesPerChannel/8));   
    auto frame = media::AudioBuffer::CopyFrom(media::SampleFormat::kSampleFormatS16,
        media::ChannelLayout::CHANNEL_LAYOUT_STEREO,
	channels, // channel count
        sampleRate, // sample rate
	samples, 
	audioDataTemp_,
	timestamp);
    PostCrossThreadTask(*main_task_runner_, FROM_HERE, CrossThreadBindOnce(&NdiInputStream::OnAudioDataReceived,WrapCrossThreadWeakPersistent(this), frame));

	//deviceOnData(deviceId.c_str(), (void *) audio_frame_16bpp_interleaved.p_data, bytesPerChannel, sampleRate, channels, samples);
    delete[] audio_frame_16bpp_interleaved.p_data;
}

void NdiInputStream::OnAudioDataReceived(scoped_refptr<media::AudioBuffer> audioBuffer) {    
    ScriptState* callback_relevant_script_state = audioCallback_->
    CallbackRelevantScriptStateOrThrowException("VideoCardAudioCallback", "handleFrame");

    if (!IsCallbackFunctionRunnable(callback_relevant_script_state,
                                  audioCallback_->IncumbentScriptState())) {
        VLOG(0) << "callback is no longer available audio NDI";
    /*
    if (!isAvailableAudioDataCallback(audioCallback_)) {
        VLOG(0) << "audio callback is no longer available NDI";
        this->disable();
        */
    } else {
        auto *frame2 = MakeGarbageCollected<AudioData>(audioBuffer);
        auto qtf = audioCallback_->handleFrame(nullptr, frame2, 0);
        qtf.IsJust(); 
    }
}

void NdiInputStream::debugFourCC(NDIlib_video_frame_v2_t video_frame) {
	std::string fourcc("unknow");
	switch (video_frame.FourCC) {
	case NDIlib_FourCC_video_type_e::NDIlib_FourCC_video_type_UYVY:
	    fourcc = "UYVY";
	    break;
	case NDIlib_FourCC_video_type_e::NDIlib_FourCC_video_type_UYVA:
	    fourcc = "UYVA";
	    break;
	case NDIlib_FourCC_video_type_e::NDIlib_FourCC_video_type_P216:
	    fourcc = "P216";
	    break;
	case NDIlib_FourCC_video_type_e::NDIlib_FourCC_video_type_PA16:
	    fourcc = "PA16";
	    break;
	case NDIlib_FourCC_video_type_e::NDIlib_FourCC_video_type_YV12:
	    fourcc = "YV12";
	    break;
	case NDIlib_FourCC_video_type_e::NDIlib_FourCC_video_type_I420:
	    fourcc = "I420";
	    break;
	case NDIlib_FourCC_video_type_e::NDIlib_FourCC_video_type_NV12:
	    fourcc = "N12";
	    break;
	case NDIlib_FourCC_video_type_e::NDIlib_FourCC_video_type_BGRA:
            fourcc = "BGRA";
	    break;
	case NDIlib_FourCC_video_type_e::NDIlib_FourCC_video_type_BGRX:
	    fourcc = "BGRX";
	    break;
	case NDIlib_FourCC_video_type_e::NDIlib_FourCC_video_type_RGBA:
            fourcc = "RGBA";
	    break;
	case NDIlib_FourCC_video_type_e::NDIlib_FourCC_video_type_RGBX:
	    fourcc = "RGBX";
	    break;
	default:
	    fourcc = "unkown max";
	    break;
	}

	VLOG(0) << "NDI Frame " << url_
			<< " " << video_frame.xres
			<< "," << video_frame.yres
			<<" fourCC " << fourcc;
}

}
