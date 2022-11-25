#include <chrono>

#include "base/logging.h"
#include "base/task/thread_pool.h"
#include "components/viz/common/gpu/raster_context_provider.h"
#include "media/base/audio_buffer.h"
#include "media/base/video_frame.h"
#include "media/base/video_util.h"
#include "ndi_output_stream.h"
#include "third_party/blink/renderer/platform/graphics/gpu/shared_gpu_context.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/convert_argb.h"
#include "third_party/libyuv/include/libyuv/rotate_argb.h"


namespace blink {

NdiOutputStream::NdiOutputStream(std::string label) {
    base::TaskTraits default_traits = {};   
    this->taskRunner_ = base::ThreadPool::CreateTaskRunner(default_traits);
    
    ndiVideoFrame_.FourCC = NDIlib_FourCC_video_type_UYVY;
    //ndiVideoFrame_.FourCC = NDIlib_FourCC_video_type_BGRA;
    ndiVideoFrame_.xres = -1;
    ndiVideoFrame_.yres = -1;
    
    NDIlib_send_create_t NDI_send_create_desc;
    NDI_send_create_desc.p_ndi_name = label.c_str();
    NDI_send_create_desc.clock_video = false;    
    NDI_send_create_desc.clock_audio = false;
    sender_ = NDIlib_send_create(&NDI_send_create_desc);
    
    videoIndex_ = 0;
    
    // TODO GC
    imagePar_.reset(new uint8_t[1920*1080*4]);
    imageImpar_.reset(new uint8_t[1920*1080*4]);
    imageRotatePar_.reset(new uint8_t[1920*1920*4]);
    imageRotateImpar_.reset(new uint8_t[1920*1920*4]);

    // TODO GC
    // 1024 muetras 2 bytes 16 canales
    audioData_ = (uint8_t*) malloc (1024*2*16);    
}

void NdiOutputStream::Trace(Visitor* visitor) const {
    ScriptWrappable::Trace(visitor);
}

void NdiOutputStream::putVideoFrame(VideoFrame* frame){    
    auto mediaFrame = frame->frame();
    auto metadata = mediaFrame->metadata();
    auto transformation = metadata.transformation;

    libyuv::RotationMode rotationMode = libyuv::kRotate0;
    int rotatedWidth = frame->codedWidth();
    int rotatedHeight = frame->codedHeight();
    if (transformation == media::VideoRotation::VIDEO_ROTATION_0){        
        rotationMode = libyuv::kRotate0;
    } else if (transformation == media::VideoRotation::VIDEO_ROTATION_90){        
        rotationMode = libyuv::kRotate90;
        rotatedWidth = frame->codedHeight();
        rotatedHeight = frame->codedWidth();
    } else if (transformation == media::VideoRotation::VIDEO_ROTATION_180){        
        rotationMode = libyuv::kRotate180;        
    } else if (transformation == media::VideoRotation::VIDEO_ROTATION_270){        
        rotationMode = libyuv::kRotate270;
        rotatedWidth = frame->codedHeight();
        rotatedHeight = frame->codedWidth();
    } 
        
    if (mediaFrame->HasGpuMemoryBuffer()) {
        //VLOG(0) << "GPU Frame";
        auto frame = media::ConvertToMemoryMappedFrame(std::move(mediaFrame));
    } else if (mediaFrame->HasTextures()){
        //VLOG(0) << "Has textures";
        auto wrapper = SharedGpuContext::ContextProviderWrapper();
        scoped_refptr<viz::RasterContextProvider> raster_provider = wrapper->ContextProvider()->RasterContextProvider();
        auto* ri = raster_provider->RasterInterface();
        auto* gr_context = raster_provider->GrContext();
        mediaFrame = media::ReadbackTextureBackedFrameToMemorySync(*mediaFrame, ri, gr_context, &videoFramePool_);
    } else {
        //VLOG(0) << "No GPU Frame";
    }

    int width = frame->codedWidth();
    int height = frame->codedHeight();
    //VLOG(0) << "NDI put video frame size: " << width << "," << height;    
    uint8_t* image = videoIndex_%2==0? imagePar_.get() : imageImpar_.get();        
    if (mediaFrame->format()==media::VideoPixelFormat::PIXEL_FORMAT_NV12) {
        //VLOG(0) << "NV12 frame";        
        ndiVideoFrame_.FourCC = NDIlib_FourCC_video_type_BGRA;
        libyuv::NV12ToARGB(mediaFrame->data(0), mediaFrame->stride(0),
            mediaFrame->data(1), mediaFrame->stride(1),
            image, width*4, width, height);
    } else if (mediaFrame->format()==media::VideoPixelFormat::PIXEL_FORMAT_I420) {
        //VLOG(0) << "I420 Frame";
        ndiVideoFrame_.FourCC = NDIlib_FourCC_video_type_BGRA;
        libyuv::I420ToARGB(mediaFrame->data(0), mediaFrame->stride(0),
		    mediaFrame->data(1), mediaFrame->stride(1),
		    mediaFrame->data(2), mediaFrame->stride(2),
		    image,
		    width*4, width, height-1);   
    } else if (mediaFrame->format()==media::VideoPixelFormat::PIXEL_FORMAT_I420A) {
        ndiVideoFrame_.FourCC = NDIlib_FourCC_video_type_BGRA;
        libyuv::I420AlphaToARGB(
            mediaFrame->data(0), mediaFrame->stride(0),
            mediaFrame->data(1), mediaFrame->stride(1),
            mediaFrame->data(2), mediaFrame->stride(2),
            mediaFrame->data(3), mediaFrame->stride(3),
            image, width*4,
            width, height, 0
        );
    } else {
        VLOG(0) << "Format " << media::VideoPixelFormatToString(mediaFrame->format());
    }
    videoIndex_++;
		   		 
    if (rotationMode != libyuv::kRotate0) { 
        uint8_t* imageRotate = videoIndex_%2==0? imageRotatePar_.get() : imageRotateImpar_.get();        
        libyuv::ARGBRotate(image, width*4,
            imageRotate, rotatedWidth*4,
            width, height,
            rotationMode);
        image = imageRotate;
    }

    ndiVideoFrame_.xres = rotatedWidth;
    ndiVideoFrame_.yres = rotatedHeight;    
    ndiVideoFrame_.p_data = image;
    NDIlib_send_send_video_async_v2(sender_, &ndiVideoFrame_);		
}

int float2intNdi(float f, int index, uint8_t *audioDataOut_) {
  uint8_t a1;
  uint8_t a2;
  int out = f * 32768;
  a1 = (uint8_t) (out >> 8 & 0xff);
  a2 = (uint8_t) (out & 0xff);
  audioDataOut_[index++] = a2;
  audioDataOut_[index++] = a1;
  return index;
}

void NdiOutputStream::putAudioFrame(NotShared<DOMFloat32Array> audio0, NotShared<DOMFloat32Array> audio1) {
    //VLOG(0) << "put audio frame";
    DOMFloat32Array* a0 = audio0.Get();
    DOMFloat32Array* a1 = audio1.Get();
    const float* aa0 = (const float*) a0->buffer()->Data();
    const float* aa1 = (const float*) a1->buffer()->Data();
    int index=0;
    for (int i=0; i<480; i++) {
      index = float2intNdi(aa0[i], index, audioData_);
      index = float2intNdi(aa1[i], index, audioData_);
    }
    
    ndiAudioFrame_.sample_rate = 48000;
    ndiAudioFrame_.no_channels = 2;
    ndiAudioFrame_.no_samples = 480;
    ndiAudioFrame_.p_data = (int16_t *) audioData_;   
    
    uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    ndiAudioFrame_.timecode = now;
    this->taskRunner_->PostTask(FROM_HERE, base::BindOnce(&NdiOutputStream::putAudioFrameInternal, WrapCrossThreadWeakPersistent(this)));    
}

void NdiOutputStream::putAudioFrameInternal() {
   NDIlib_util_send_send_audio_interleaved_16s(sender_, &ndiAudioFrame_);    
}

}
