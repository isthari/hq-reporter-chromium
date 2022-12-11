#include "decklink_output_stream.h"

#include "components/viz/common/gpu/raster_context_provider.h"
#include "media/base/video_util.h"
#include "third_party/blink/renderer/platform/graphics/gpu/shared_gpu_context.h"
#include "third_party/libyuv/include/libyuv.h"

namespace blink {

DecklinkOutputStream::DecklinkOutputStream(IDeckLinkOutput *decklinkOutput,
            IDeckLinkDisplayMode* displayMode)
    : decklinkOutput_(decklinkOutput),
    frameCounter_(0)
{
    width_ = (int) displayMode->GetWidth();
    height_ = (int) displayMode->GetHeight();

    // TODO GC    
    // TODO control size
    i420originalY_ = (uint8_t*) malloc(1920*1920);
    i420originalU_ = (uint8_t*) malloc(1920*1920/2);
    i420originalV_ = (uint8_t*) malloc(1920*1920/2);    

    // TODO GC
    scaledStrideY_ = width_;
    scaledStrideU_ = width_/2;
    scaledStrideV_ = width_/2;
    scaledY_ = (uint8_t*) malloc(scaledStrideY_*height_);
    scaledU_ = (uint8_t*) malloc(scaledStrideU_*height_);   
    scaledV_ = (uint8_t*) malloc(scaledStrideV_*height_);       

    // TODO GC
    VLOG(0) << "create video frame " << width_ << "x" << height_;
    decklinkOutput_->CreateVideoFrame((int32_t) width_,
		    	(int32_t) height_,
		    	(int32_t) width_*2, // asume siempre 8 bit YUV
		    	bmdFormat8BitYUV,
		    	bmdFrameFlagDefault,
		    	&playbackFrame_);

    /*
    FRAMERATE NO SE USA
    displayMode->GetFrameRate(&frameDuration_, &frameTimescale_);    
    */

    // hacer un disable seguido de un enable
    //decklinkOutput_->DisableVideoOutput();
    //decklinkOutput_->DisableAudioOutput();

    // TODO sacarlo a una función a parte de inicializacion
    BMDDisplayMode bmdMode = displayMode->GetDisplayMode();   
    HRESULT video = decklinkOutput_->EnableVideoOutput(bmdMode, bmdVideoOutputRP188);
    if (video == S_OK) {
        VLOG(0) << "enable video output ok";
    } else {
        VLOG(0) << "enable video output error";
    }

    // TODO sacarlo a una función a parte de inicialización
    int audioChannels = 2;
    HRESULT audio = decklinkOutput_->EnableAudioOutput(bmdAudioSampleRate48kHz, bmdAudioSampleType16bitInteger, (uint32_t) audioChannels, bmdAudioOutputStreamContinuous);
    if (audio == S_OK) {
        VLOG(0) << "enable audio output ok";        
    } else {
        VLOG(0) << "enable audio output error";
    }
}

void DecklinkOutputStream::Trace(Visitor* visitor) const {
    ScriptWrappable::Trace(visitor);
}

void DecklinkOutputStream::putVideoFrame(VideoFrame* frame) {
    //VLOG(0) << "put video frame (" << frame->codedWidth() << "," << frame->codedHeight() << ")";
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

    bool log = false;
    if (frameCounter_++ % 30 == 0){
        log = true;
        VLOG(0) << "put video frame (" << frame->codedWidth() << "," << frame->codedHeight() << ")";
    }
    
    gfx::Size size(height_, width_);    

    uint8_t *deckLinkBuffer = nullptr;
    playbackFrame_->GetBytes((void**) &deckLinkBuffer);

    // set black image to fix offset
    memset(scaledY_, 0, scaledStrideY_ * height_);   
    memset(scaledU_, 128, scaledStrideU_ * height_);
    memset(scaledV_, 128, scaledStrideV_ * height_);
    
    uint8_t* nextY;
    uint8_t* nextU;
    uint8_t* nextV;
    int nextStrideY;
    int nextStrideU;
    int nextStrideV;
    int currentWidth = frame->codedWidth();
    int currentHeight = frame->codedHeight();    

    //if (mediaFrame->HasGpuMemoryBuffer()) {
        //VLOG(0) << "GPU Frame";        
    //} else 
    if (mediaFrame->HasTextures()){
        //VLOG(0) << "Has textures";
        auto mediaFrame = frame->frame();
        // TODO llevar el primer bloque a una funcion helper para reutilizar con NDO
        auto wrapper = SharedGpuContext::ContextProviderWrapper();
        scoped_refptr<viz::RasterContextProvider> raster_provider = wrapper->ContextProvider()->RasterContextProvider();
        auto* ri = raster_provider->RasterInterface();
        auto* gr_context = raster_provider->GrContext();
        mediaFrame = media::ReadbackTextureBackedFrameToMemorySync(*mediaFrame, ri, gr_context, &videoFramePool_);

        nextY = i420originalY_;
        nextU = i420originalU_;
        nextV = i420originalV_;
        if (rotationMode == libyuv::kRotate0) {            
            nextStrideY = frame->codedWidth();
            nextStrideU = frame->codedWidth()/2;
            nextStrideV = frame->codedWidth()/2;

            libyuv::NV12ToI420(mediaFrame->data(0), mediaFrame->stride(0),
                    mediaFrame->data(1), mediaFrame->stride(1),
                    i420originalY_, frame->codedWidth(),
                    i420originalU_, frame->codedWidth()/2,
                    i420originalV_, frame->codedWidth()/2, 
                    frame->codedWidth(), frame->codedHeight());
        } else {
            nextStrideY = rotatedWidth;
            nextStrideU = rotatedWidth/2;
            nextStrideV = rotatedWidth/2;
            currentWidth = rotatedWidth;
            currentHeight = rotatedHeight;

            libyuv::NV12ToI420Rotate(
                    mediaFrame->data(0), mediaFrame->stride(0),
                    mediaFrame->data(1), mediaFrame->stride(1),
                    i420originalY_, nextStrideY,
                    i420originalU_, nextStrideU,
                    i420originalV_, nextStrideV,
                    frame->codedWidth(), frame->codedHeight(),
                    rotationMode);
        }      
    } else {
        //VLOG(0) << "No GPU Frame";
        // TODO comprobar cuando hay que escalar o no
        if (rotationMode == libyuv::kRotate0) {
            nextY = mediaFrame->data(0);
            nextU = mediaFrame->data(1);
            nextV = mediaFrame->data(2);
            nextStrideY = mediaFrame->stride(0);
            nextStrideU = mediaFrame->stride(1);
            nextStrideV = mediaFrame->stride(2);            
        } else {
            nextY = i420originalY_;
            nextU = i420originalU_;
            nextV = i420originalV_;
            nextStrideY = rotatedWidth;
            nextStrideU = rotatedWidth/2;
            nextStrideV = rotatedWidth/2;
            currentWidth = rotatedWidth;
            currentHeight = rotatedHeight;

            libyuv::I420Rotate(mediaFrame->data(0), mediaFrame->stride(0),
                mediaFrame->data(1), mediaFrame->stride(1),
		        mediaFrame->data(2), mediaFrame->stride(2),
                i420originalY_, nextStrideY,
                i420originalU_, nextStrideU,
                i420originalV_, nextStrideV,
                frame->codedWidth(), frame->codedHeight(),
                rotationMode);                        
        }        
    }

    double factorOriginal = ((double)currentWidth) / ((double)currentHeight);
    double factorOutput = ((double)width_) / ((double)height_);

    int scaledWidth = -1;
    int scaledHeight = -1;
    int offsetX = 0;
    int offsetY = 0;
    std::string limitedBy;
    if (factorOriginal > factorOutput) {        
        limitedBy = "width";
        scaledWidth = width_;
        scaledHeight = ((double) currentHeight) / (((double)currentWidth) / ((double) width_));
        offsetY = ((double)(height_ - scaledHeight)) / 2.0;
    } else {
        limitedBy = "height";        
        scaledHeight = height_;
        scaledWidth = ((double) currentWidth) / (((double)currentHeight) / ((double) height_));        
        offsetX = ((double)(width_ - scaledWidth)) / 2.0;
    }

    if (log){
        VLOG(0) << "Factor original " << factorOriginal << " factor output " << factorOutput << " limited by "+limitedBy;
        VLOG(0) << "Scaled width " << scaledWidth << " scaled height " << scaledHeight;
        VLOG(0) << "offset X " << offsetX << " offset Y " << offsetY;
    }

    libyuv::I420Scale(
            nextY, nextStrideY,
		    nextU, nextStrideU,
		    nextV, nextStrideV,
		    currentWidth, currentHeight,
		    scaledY_ + offsetX + (offsetY*scaledStrideY_), scaledStrideY_, 		
		    scaledU_ + offsetX/2 + (offsetY*scaledStrideU_/2), scaledStrideU_,
		    scaledV_ + offsetX/2 + (offsetY*scaledStrideU_/2), scaledStrideV_,
		    scaledWidth, scaledHeight,
		    libyuv::FilterMode::kFilterBox);

    libyuv::I420ToUYVY(scaledY_, scaledStrideY_,
            scaledU_, scaledStrideU_,
            scaledV_, scaledStrideV_,
            deckLinkBuffer, width_*2,             
            width_, height_-1);    
    decklinkOutput_->DisplayVideoFrameSync(playbackFrame_);
}

void DecklinkOutputStream::sendBlackFrame(){    
    memset(scaledY_, 0, scaledStrideY_ * height_);
    memset(scaledU_, 128, scaledStrideU_ * height_);
    memset(scaledV_, 128, scaledStrideV_ * height_);        

    uint8_t *deckLinkBuffer = nullptr;
    playbackFrame_->GetBytes((void**) &deckLinkBuffer);        
    libyuv::I420ToUYVY(scaledY_, scaledStrideY_,
    			scaledU_, scaledStrideU_,
    			scaledV_, scaledStrideY_,
			deckLinkBuffer,
			width_*2, width_, height_-1);
    decklinkOutput_->DisplayVideoFrameSync(playbackFrame_);    
}


}