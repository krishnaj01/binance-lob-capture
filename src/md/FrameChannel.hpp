#pragma once

#include <cstddef>

#include "common/SpscQueue.hpp"
#include "md/Message.hpp"

namespace blc {

// Network thread <-> processor thread hand-off, doubling as a buffer pool.
//
//   filled   : network thread -> processor   (frames to process)
//   recycled : processor -> network thread   (processed frames, payload capacity intact)
//
// The network thread takes a recycled buffer before copying a frame in, so after warm-up
// no frame needs a heap allocation, however deep `filled` is. Both queues are SPSC: all
// WebSocket clients run on the single io_context thread.
struct FrameChannel {
    explicit FrameChannel(std::size_t capacity) : filled(capacity), recycled(capacity) {}

    SpscQueue<RawFrame> filled;
    SpscQueue<RawFrame> recycled;
};

}  // namespace blc
