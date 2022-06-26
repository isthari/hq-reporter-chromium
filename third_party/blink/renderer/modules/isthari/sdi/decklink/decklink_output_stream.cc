#include "decklink_output_stream.h"

#include "third_party/libyuv/include/libyuv.h"

namespace blink {

DecklinkOutputStream::DecklinkOutputStream(IDeckLinkOutput *decklinkOutput,
            IDeckLinkDisplayMode* displayMode)
    : decklinkOutput_(decklinkOutput)
{
    width_ = (int) displayMode->GetWidth();
    height_ = (int) displayMode->GetHeight();

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
    gfx::Size size(height_, width_);
    auto mediaFrame = frame->frame();

    uint8_t *deckLinkBuffer = nullptr;
    playbackFrame_->GetBytes((void**) &deckLinkBuffer);

    // Este bloque permite saber cuando viene de decodificacion por HW
    /*
    if (mediaFrame->format()==media::VideoPixelFormat::PIXEL_FORMAT_NV12) {
        int strideY = mediaFrame->stride(0);
        int strideUV = mediaFrame->stride(1);
        LOG(ERROR) << "strideY "<< strideY << " strideUV " << strideUV;
    } 
    */

    // TODO comprobar cuando hay que escalar o no
    libyuv::I420Scale(mediaFrame->data(0), mediaFrame->stride(0),
		mediaFrame->data(1), mediaFrame->stride(1),
		mediaFrame->data(2), mediaFrame->stride(2),
		frame->codedWidth(), frame->codedHeight(),
		scaledY_, scaledStrideY_, 		
		scaledU_, scaledStrideU_,
		scaledV_, scaledStrideV_,
		width_, height_,
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