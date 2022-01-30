#include "video_card_mode.h"

namespace blink {

VideoCardMode::VideoCardMode(std::string name, long width, long height, double fps)
	: name_(name),
	  width_(width),
  	  height_(height),
	  fps_(fps)	  
{
}

void VideoCardMode::Trace(Visitor* visitor) const {
    ScriptWrappable::Trace(visitor);    
}

} // blink
