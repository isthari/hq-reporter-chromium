#pragma once

#include "media/base/audio_buffer.h"

namespace blink {

// namespace
class LinkedList {
public:
    LinkedList();
    int channels;
    int samples;
    uint8_t** audioBuffer_;
    int index;
    base::TimeDelta timestamp;

    LinkedList* next;
};

}