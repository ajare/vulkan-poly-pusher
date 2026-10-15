#pragma once
#include "PipelineStore.h"

namespace mpp::detail {
// Native snapshots, never client object pointers. Pins protect queued storage;
// submission serials protect it after submission. Descriptor snapshots are
// retained by the submitting frame context until its fence completes.
struct Draw {
    PipelineStore::DrawBinding program;
    std::vector<VkBuffer> vertices;
    VkBuffer index{};
    VkIndexType indexType{};
    uint32_t first{}, count{}, instances{};
};
}
