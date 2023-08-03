#pragma once

#include <string>
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class VideoCardMode : public ScriptWrappable
{		  
    DEFINE_WRAPPERTYPEINFO();
public:
    VideoCardMode(std::string name, 
    	long width, long height, 
    	long frameRateDuration, long frameRateScale,
    	double fps);    
    void Trace(Visitor*) const override;
    String name() { return String(name_); }
    long width() { return width_; }
    long height() { return height_; }
    long frameRateDuration() { return frameRateDuration_; }
    long frameRateScale() { return frameRateScale_; }
    double fps() { return fps_; }    

private:
    std::string name_;
    long width_;
    long height_;
    long frameRateDuration_;
    long frameRateScale_;
    double fps_;
};

} // namespace blink
