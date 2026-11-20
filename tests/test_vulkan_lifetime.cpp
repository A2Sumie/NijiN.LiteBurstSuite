// Standalone Vulkan buffer-lifetime & stack-use-after-scope regression suite.
// Compiles heraw_gpu_vulkan.hpp against a strict handle-tracking mock Vulkan ICD
// so multi-tile 2x readback (core_y0 > 0) and mid-flight OOM unwind are verified
// deterministically under ASan (-fsanitize-address-use-after-scope) and -O3.

#define HERAW_VULKAN_HOST_TEST 1
#include "../heraw_gpu_backend.hpp"
#include "../heraw_gpu_vulkan.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace mock_vk {

struct MockMem {
    VkDeviceSize size = 0;
    std::vector<uint8_t> data;
    bool mapped = false;
};

struct MockBuf {
    VkDeviceSize size = 0;
    uint64_t bound_mem = 0;
};

struct State {
    std::mutex mtx;
    uint64_t next_id = 1;
    uint64_t buf_creates = 0;
    uint64_t fail_from_buf = 0;
    uint64_t mem_allocs = 0;
    uint64_t fail_single_mem = 0;
    uint64_t bad_ops = 0;
    std::string first_bad_reason;
    std::unordered_map<uint64_t, MockBuf> live_bufs;
    std::unordered_map<uint64_t, MockMem> live_mems;

    void flag_bad(const std::string& msg) {
        if (bad_ops == 0) first_bad_reason = msg;
        ++bad_ops;
    }

    void reset_counters(uint64_t fail_buf = 0, uint64_t fail_mem = 0) {
        std::lock_guard<std::mutex> lk(mtx);
        buf_creates = 0;
        fail_from_buf = fail_buf;
        mem_allocs = 0;
        fail_single_mem = fail_mem;
        bad_ops = 0;
        first_bad_reason.clear();
    }
};

static State& st() {
    static State s;
    return s;
}

template <typename H>
static uint64_t to_u64(H h) {
    return reinterpret_cast<uint64_t>(h);
}

template <typename H>
static H from_u64(uint64_t v) {
    return reinterpret_cast<H>(static_cast<uintptr_t>(v));
}

static VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance* pInstance) {
    *pInstance = from_u64<VkInstance>(0x11110001ULL);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance, const VkAllocationCallbacks*) {}

static VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance, uint32_t* pCount, VkPhysicalDevice* pDevices) {
    if (!pDevices) {
        *pCount = 1;
        return VK_SUCCESS;
    }
    if (*pCount >= 1) {
        pDevices[0] = from_u64<VkPhysicalDevice>(0x22220001ULL);
        *pCount = 1;
    }
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice, VkPhysicalDeviceProperties* pProps) {
    std::memset(pProps, 0, sizeof(*pProps));
    pProps->apiVersion = VK_MAKE_VERSION(1, 2, 0);
    pProps->vendorID = 0x10DE;
    pProps->deviceID = 0x2702;
    pProps->deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    std::snprintf(pProps->deviceName, sizeof(pProps->deviceName), "Mock Discrete Vulkan GPU");
    pProps->limits.maxComputeSharedMemorySize = 49152;
    pProps->limits.maxStorageBufferRange = 512u * 1024u * 1024u;
}

static VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* pMem) {
    std::memset(pMem, 0, sizeof(*pMem));
    pMem->memoryHeapCount = 2;
    pMem->memoryHeaps[0].size = 8ULL << 30;
    pMem->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
    pMem->memoryHeaps[1].size = 16ULL << 30;
    pMem->memoryHeaps[1].flags = 0;
    pMem->memoryTypeCount = 2;
    pMem->memoryTypes[0].heapIndex = 0;
    pMem->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    pMem->memoryTypes[1].heapIndex = 1;
    pMem->memoryTypes[1].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
}

static VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice, uint32_t* pCount, VkQueueFamilyProperties* pProps) {
    if (!pProps) {
        *pCount = 1;
        return;
    }
    if (*pCount >= 1) {
        std::memset(&pProps[0], 0, sizeof(pProps[0]));
        pProps[0].queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
        pProps[0].queueCount = 1;
        *pCount = 1;
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice* pDevice) {
    *pDevice = from_u64<VkDevice>(0x33330001ULL);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice, const VkAllocationCallbacks*) {}

static VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice, uint32_t, uint32_t, VkQueue* pQueue) {
    *pQueue = from_u64<VkQueue>(0x44440001ULL);
}

static VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(
    VkDevice, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks*, VkBuffer* pBuffer) {
    auto& s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    ++s.buf_creates;
    if (s.fail_from_buf > 0 && s.buf_creates >= s.fail_from_buf) {
        *pBuffer = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    const uint64_t h = 0xB0FF000000000000ULL | (++s.next_id);
    s.live_bufs.emplace(h, MockBuf{pCreateInfo->size, 0});
    *pBuffer = from_u64<VkBuffer>(h);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(
    VkDevice, VkBuffer buffer, const VkAllocationCallbacks*) {
    if (buffer == VK_NULL_HANDLE) return;
    auto& s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    const uint64_t h = to_u64(buffer);
    auto it = s.live_bufs.find(h);
    if (it == s.live_bufs.end()) {
        char msg[128];
        std::snprintf(msg, sizeof(msg), "vkDestroyBuffer called with invalid/dead handle 0x%llx",
                      static_cast<unsigned long long>(h));
        s.flag_bad(msg);
        return;
    }
    s.live_bufs.erase(it);
}

static VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(
    VkDevice, VkBuffer buffer, VkMemoryRequirements* pReq) {
    auto& s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    const uint64_t h = to_u64(buffer);
    auto it = s.live_bufs.find(h);
    std::memset(pReq, 0, sizeof(*pReq));
    if (it == s.live_bufs.end()) {
        s.flag_bad("vkGetBufferMemoryRequirements invalid buffer");
        pReq->size = 64;
    } else {
        pReq->size = (it->second.size + 255) & ~VkDeviceSize(255);
    }
    pReq->alignment = 256;
    pReq->memoryTypeBits = 0x3u;
}

static VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(
    VkDevice, const VkMemoryAllocateInfo* pAllocInfo, const VkAllocationCallbacks*, VkDeviceMemory* pMem) {
    auto& s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    ++s.mem_allocs;
    if ((s.fail_from_buf > 0 && s.buf_creates >= s.fail_from_buf) ||
        (s.fail_single_mem > 0 && s.mem_allocs == s.fail_single_mem)) {
        *pMem = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    const uint64_t h = 0xDE00000000000000ULL | (++s.next_id);
    MockMem mm;
    mm.size = pAllocInfo->allocationSize;
    if (pAllocInfo->memoryTypeIndex == 1) {
        mm.data.assign(static_cast<size_t>(mm.size), 0u);
    }
    s.live_mems.emplace(h, std::move(mm));
    *pMem = from_u64<VkDeviceMemory>(h);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice, VkDeviceMemory mem, const VkAllocationCallbacks*) {
    if (mem == VK_NULL_HANDLE) return;
    auto& s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    const uint64_t h = to_u64(mem);
    auto it = s.live_mems.find(h);
    if (it == s.live_mems.end()) {
        char msg[128];
        std::snprintf(msg, sizeof(msg), "vkFreeMemory called with invalid/dead handle 0x%llx",
                      static_cast<unsigned long long>(h));
        s.flag_bad(msg);
        return;
    }
    if (it->second.mapped) {
        s.flag_bad("vkFreeMemory called while memory still mapped");
    }
    s.live_mems.erase(it);
}

static VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(
    VkDevice, VkBuffer buffer, VkDeviceMemory mem, VkDeviceSize) {
    auto& s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    auto bit = s.live_bufs.find(to_u64(buffer));
    auto mit = s.live_mems.find(to_u64(mem));
    if (bit == s.live_bufs.end() || mit == s.live_mems.end()) {
        s.flag_bad("vkBindBufferMemory invalid handle");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    bit->second.bound_mem = mit->first;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(
    VkDevice, VkDeviceMemory mem, VkDeviceSize offset, VkDeviceSize, VkMemoryMapFlags, void** ppData) {
    auto& s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    auto mit = s.live_mems.find(to_u64(mem));
    if (mit == s.live_mems.end()) {
        s.flag_bad("vkMapMemory invalid handle");
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    if (mit->second.mapped) {
        s.flag_bad("vkMapMemory double map");
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    if (mit->second.data.empty()) {
        mit->second.data.assign(static_cast<size_t>(mit->second.size), 0u);
    }
    mit->second.mapped = true;
    *ppData = mit->second.data.data() + static_cast<size_t>(offset);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice, VkDeviceMemory mem) {
    auto& s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    auto mit = s.live_mems.find(to_u64(mem));
    if (mit == s.live_mems.end() || !mit->second.mapped) {
        s.flag_bad("vkUnmapMemory invalid or unmapped memory");
        return;
    }
    mit->second.mapped = false;
}

static VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(
    VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule* pMod) {
    *pMod = from_u64<VkShaderModule>(0x55550001ULL);
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice, VkShaderModule, const VkAllocationCallbacks*) {}

static VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(
    VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout* pOut) {
    *pOut = from_u64<VkDescriptorSetLayout>(0x66660001ULL);
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*) {}

static VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(
    VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout* pOut) {
    *pOut = from_u64<VkPipelineLayout>(0x77770001ULL);
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice, VkPipelineLayout, const VkAllocationCallbacks*) {}

static VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(
    VkDevice, VkPipelineCache, uint32_t count, const VkComputePipelineCreateInfo*,
    const VkAllocationCallbacks*, VkPipeline* pPipelines) {
    for (uint32_t i = 0; i < count; ++i) {
        pPipelines[i] = from_u64<VkPipeline>(0x88880000ULL + i + 1);
    }
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice, VkPipeline, const VkAllocationCallbacks*) {}

static VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(
    VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool* pOut) {
    *pOut = from_u64<VkDescriptorPool>(0x99990001ULL);
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(VkDevice, VkDescriptorPool, const VkAllocationCallbacks*) {}
static VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice, VkDescriptorPool, VkDescriptorPoolResetFlags) {
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(
    VkDevice, const VkDescriptorSetAllocateInfo* pInfo, VkDescriptorSet* pSets) {
    for (uint32_t i = 0; i < pInfo->descriptorSetCount; ++i) {
        pSets[i] = from_u64<VkDescriptorSet>(0xAAAA0001ULL + i);
    }
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice, uint32_t writeCount, const VkWriteDescriptorSet* pWrites, uint32_t, const VkCopyDescriptorSet*) {
    auto& s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    for (uint32_t i = 0; i < writeCount; ++i) {
        if (pWrites[i].pBufferInfo) {
            const uint64_t bh = to_u64(pWrites[i].pBufferInfo->buffer);
            if (s.live_bufs.find(bh) == s.live_bufs.end()) {
                s.flag_bad("vkUpdateDescriptorSets bound dead/invalid VkBuffer");
            }
        }
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool* pOut) {
    *pOut = from_u64<VkCommandPool>(0xBBBB0001ULL);
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice, VkCommandPool, const VkAllocationCallbacks*) {}
static VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice, const VkCommandBufferAllocateInfo* pInfo, VkCommandBuffer* pCmds) {
    for (uint32_t i = 0; i < pInfo->commandBufferCount; ++i) {
        pCmds[i] = from_u64<VkCommandBuffer>(0xCCCC0001ULL + i);
    }
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer, VkCommandBufferResetFlags) { return VK_SUCCESS; }
static VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer, const VkCommandBufferBeginInfo*) { return VK_SUCCESS; }
static VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer) { return VK_SUCCESS; }
static VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer, VkPipelineBindPoint, VkPipeline) {}
static VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(
    VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*) {}
static VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(
    VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags, uint32_t, uint32_t, const void*) {}
static VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer, uint32_t, uint32_t, uint32_t) {}
static VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags,
    uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*, uint32_t, const VkImageMemoryBarrier*) {}

static VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(
    VkCommandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) {
    auto& s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    auto sit = s.live_bufs.find(to_u64(srcBuffer));
    auto dit = s.live_bufs.find(to_u64(dstBuffer));
    if (sit == s.live_bufs.end() || dit == s.live_bufs.end()) {
        s.flag_bad("vkCmdCopyBuffer dead/invalid VkBuffer");
        return;
    }
    for (uint32_t i = 0; i < regionCount; ++i) {
        const auto& r = pRegions[i];
        if (r.srcOffset + r.size > sit->second.size || r.dstOffset + r.size > dit->second.size) {
            s.flag_bad("vkCmdCopyBuffer out-of-bounds copy region");
            return;
        }
        auto dmem = s.live_mems.find(dit->second.bound_mem);
        if (dmem != s.live_mems.end() && !dmem->second.data.empty()) {
            auto smem = s.live_mems.find(sit->second.bound_mem);
            if (smem != s.live_mems.end() && !smem->second.data.empty()) {
                std::memcpy(dmem->second.data.data() + static_cast<size_t>(r.dstOffset),
                            smem->second.data.data() + static_cast<size_t>(r.srcOffset),
                            static_cast<size_t>(r.size));
            } else {
                std::memset(dmem->second.data.data() + static_cast<size_t>(r.dstOffset), 0,
                            static_cast<size_t>(r.size));
            }
        }
    }
}

static VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(
    VkCommandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t) {
    auto& s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    auto dit = s.live_bufs.find(to_u64(dstBuffer));
    if (dit == s.live_bufs.end()) {
        s.flag_bad("vkCmdFillBuffer dead/invalid VkBuffer");
        return;
    }
    if (size != VK_WHOLE_SIZE && dstOffset + size > dit->second.size) {
        s.flag_bad("vkCmdFillBuffer out-of-bounds range");
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(
    VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence* pOut) {
    *pOut = from_u64<VkFence>(0xDDDD0001ULL);
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice, VkFence, const VkAllocationCallbacks*) {}
static VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice, uint32_t, const VkFence*) { return VK_SUCCESS; }
static VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t) { return VK_SUCCESS; }
static VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue, uint32_t, const VkSubmitInfo*, VkFence) { return VK_SUCCESS; }

#define MATCH_FN(fn) if (std::strcmp(pName, #fn) == 0) return reinterpret_cast<PFN_vkVoidFunction>(&fn)

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice, const char* pName) {
    MATCH_FN(vkDestroyDevice);
    MATCH_FN(vkGetDeviceQueue);
    MATCH_FN(vkCreateBuffer);
    MATCH_FN(vkDestroyBuffer);
    MATCH_FN(vkGetBufferMemoryRequirements);
    MATCH_FN(vkAllocateMemory);
    MATCH_FN(vkFreeMemory);
    MATCH_FN(vkBindBufferMemory);
    MATCH_FN(vkMapMemory);
    MATCH_FN(vkUnmapMemory);
    MATCH_FN(vkCreateShaderModule);
    MATCH_FN(vkDestroyShaderModule);
    MATCH_FN(vkCreateDescriptorSetLayout);
    MATCH_FN(vkDestroyDescriptorSetLayout);
    MATCH_FN(vkCreatePipelineLayout);
    MATCH_FN(vkDestroyPipelineLayout);
    MATCH_FN(vkCreateComputePipelines);
    MATCH_FN(vkDestroyPipeline);
    MATCH_FN(vkCreateDescriptorPool);
    MATCH_FN(vkDestroyDescriptorPool);
    MATCH_FN(vkResetDescriptorPool);
    MATCH_FN(vkAllocateDescriptorSets);
    MATCH_FN(vkUpdateDescriptorSets);
    MATCH_FN(vkCreateCommandPool);
    MATCH_FN(vkDestroyCommandPool);
    MATCH_FN(vkAllocateCommandBuffers);
    MATCH_FN(vkResetCommandBuffer);
    MATCH_FN(vkBeginCommandBuffer);
    MATCH_FN(vkEndCommandBuffer);
    MATCH_FN(vkCmdBindPipeline);
    MATCH_FN(vkCmdBindDescriptorSets);
    MATCH_FN(vkCmdPushConstants);
    MATCH_FN(vkCmdDispatch);
    MATCH_FN(vkCmdPipelineBarrier);
    MATCH_FN(vkCmdCopyBuffer);
    MATCH_FN(vkCmdFillBuffer);
    MATCH_FN(vkCreateFence);
    MATCH_FN(vkDestroyFence);
    MATCH_FN(vkResetFences);
    MATCH_FN(vkWaitForFences);
    MATCH_FN(vkQueueSubmit);
    return nullptr;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char* pName) {
    MATCH_FN(vkCreateInstance);
    MATCH_FN(vkDestroyInstance);
    MATCH_FN(vkEnumeratePhysicalDevices);
    MATCH_FN(vkGetPhysicalDeviceProperties);
    MATCH_FN(vkGetPhysicalDeviceMemoryProperties);
    MATCH_FN(vkGetPhysicalDeviceQueueFamilyProperties);
    MATCH_FN(vkCreateDevice);
    MATCH_FN(vkGetDeviceProcAddr);
    return nullptr;
}

#undef MATCH_FN

} // namespace mock_vk

static void require_clean(const char* label) {
    auto& s = mock_vk::st();
    std::lock_guard<std::mutex> lk(s.mtx);
    if (s.bad_ops > 0) {
        throw std::runtime_error(std::string(label) + ": " + s.first_bad_reason);
    }
    if (!s.live_bufs.empty() || !s.live_mems.empty()) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s: leaked %zu VkBuffers and %zu VkDeviceMemories",
                      label, s.live_bufs.size(), s.live_mems.size());
        throw std::runtime_error(buf);
    }
}

struct Stage45Fixture {
    heraw_gpu::Stage45GpuJob j;
    std::vector<std::vector<float>> frames, fy, fx, mw;
    std::vector<float> cov, guide, anchor, ha, boost, rgb, fcomp, eta;
    std::vector<uint32_t> rl;

    Stage45Fixture(int H, int W, int N = 5)
        : frames(size_t(N), std::vector<float>(size_t(H) * W, 0.22f)),
          fy(size_t(N), std::vector<float>(size_t(H / 2) * (W / 2), 0.12f)),
          fx(size_t(N), std::vector<float>(size_t(H / 2) * (W / 2), -0.09f)),
          mw(size_t(N), std::vector<float>(size_t(H / 2) * (W / 2), 1.0f)),
          cov(size_t(H / 2) * (W / 2) * 3, 1.5f),
          guide(size_t(H / 2) * (W / 2), 0.22f),
          anchor(size_t(H) * W, 0.22f),
          ha(size_t(H) * W, 0.22f),
          boost(size_t(H / 2) * (W / 2), 1.0f),
          rgb(size_t(H) * W * 12, 0.0f),
          fcomp(size_t(H) * W * 4, 0.0f),
          eta(size_t((H + 3) / 4) * ((W + 3) / 4), 0.0f),
          rl(size_t(H / 2) * (W / 2), heraw_gpu::pack_half2_u32(1.0f, 1.0f)) {
        std::fill(fy[0].begin(), fy[0].end(), 0.0f);
        std::fill(fx[0].begin(), fx[0].end(), 0.0f);
        j.H1 = H;
        j.W1 = W;
        j.N = N;
        j.keep_frames_lin = true;
        j.frames_lin = &frames;
        j.flows_fy = &fy;
        j.flows_fx = &fx;
        j.mw_half = &mw;
        j.inv_cov = cov.data();
        j.g_guide = guide.data();
        j.c_anc_1x = anchor.data();
        j.g0_ha_1x = ha.data();
        j.snr_boost_half = boost.data();
        j.rl_mod_f16 = rl.data();
        j.rgb_2x_out = rgb.data();
        j.fisher_comp_out = fcomp.data();
        j.eta_quarter_out = eta.data();
        j.pg_sig0_sq = 1e-5;
        j.pg_gain = 1e-4;
        j.denoise_strength = 0.85f;
        j.chroma_denoise = 0.80f;
        j.psf_iters = 2;
        j.psf_sigma = 0.85f;
        j.n_threads = 2;
    }
};

static void test_v3_multitile_readback() {
    for (bool defer_psf : {true, false}) {
        for (bool is_1x : {false, true}) {
            Stage45Fixture f(640, 320, 5);
            f.j.math_v3_unified = true;
            f.j.is_1x_mode = is_1x;
            f.j.defer_optical_psf = defer_psf;
            f.j.tile_size_1x = 240; // Forces 3 vertical tiles; tiles 1 & 2 have core_y0 > 0 (sr0_base > 0)
            mock_vk::st().reset_counters();
            if (!heraw_gpu::run_stage45_gpu(f.j)) {
                throw std::runtime_error("run_stage45_gpu V3 failed: " + f.j.out_err);
            }
            if (f.j.out_tile_count < 2) {
                throw std::runtime_error("Expected multi-tile V3 execution");
            }
            require_clean("v3_multitile_readback");
        }
    }
    std::puts("v3_multitile_readback=PASS");
}

static void test_legacy_multitile_readback() {
    for (bool astro : {false, true}) {
        Stage45Fixture f(640, 320, 4);
        f.j.math_v3_unified = false;
        f.j.is_astro = astro; // astro=true exercises std::swap(b_dR, b_tmpR)
        f.j.tile_size_1x = 240;
        mock_vk::st().reset_counters();
        if (!heraw_gpu::run_stage45_gpu(f.j)) {
            throw std::runtime_error("run_stage45_gpu legacy failed: " + f.j.out_err);
        }
        if (f.j.out_tile_count < 2) {
            throw std::runtime_error("Expected multi-tile legacy execution");
        }
        require_clean("legacy_multitile_readback");
    }
    std::puts("legacy_multitile_readback=PASS");
}

static void test_other_vulkan_entry_points() {
    // 1. Sensor residual
    {
        Stage45Fixture f(64, 64, 4);
        std::vector<float> scene(32 * 32 * 3, 0.1f);
        auto res = f.frames;
        heraw_gpu::SensorResidualJob sj{
            64, 64, &f.frames, &f.fy, &f.fx, &f.mw,
            scene.data(), &f.j.cfa6x6[0][0], 1e-6f, 1e-4f, &res
        };
        mock_vk::st().reset_counters();
        if (!heraw_gpu::run_sensor_residual_gpu(sj)) {
            throw std::runtime_error("run_sensor_residual_gpu failed");
        }
        require_clean("sensor_residual");
    }
    // 2. M5-A GLS
    for (int sc : {1, 2}) {
        Stage45Fixture f(64, 64, 4);
        heraw_gpu::M5aGlsGpuJob mj;
        mj.H1 = 64; mj.W1 = 64; mj.N = 4; mj.out_scale = sc;
        mj.iterations = 2; mj.jacobian_prox_strength = 0.35f;
        std::memcpy(mj.cfa6x6, f.j.cfa6x6, sizeof(mj.cfa6x6));
        mj.raw = &f.frames; mj.fy = &f.fy; mj.fx = &f.fx; mj.mw = &f.mw;
        mj.fisher_comp = f.fcomp.data();
        mj.rgb = f.rgb.data();
        mock_vk::st().reset_counters();
        if (!heraw_gpu::run_m5a_gls_gpu(mj)) {
            throw std::runtime_error("run_m5a_gls_gpu failed: " + mj.out_err);
        }
        require_clean("m5a_gls");
    }
    // 3. M5-B Deconv
    for (bool astro : {false, true}) {
        std::vector<float> rgb(96 * 96 * 3, 0.15f);
        std::vector<float> focus(48 * 48, 0.9f);
        heraw_gpu::M5bDeconvGpuJob dj;
        dj.rgb = rgb.data(); dj.H = 96; dj.W = 96;
        dj.out_scale = 2; dj.psf_iters = 3; dj.strength = 0.8f;
        dj.is_astro = astro;
        dj.focus_half = focus.data(); dj.focus_h = 48; dj.focus_w = 48;
        heraw_gpu::M5bZoneParam z;
        z.sigma_major_px = 1.2f; z.sigma_minor_px = 1.0f;
        z.sigma_rgb_px[0] = z.sigma_rgb_px[1] = z.sigma_rgb_px[2] = 1.2f;
        dj.zones.push_back(z);
        mock_vk::st().reset_counters();
        if (!heraw_gpu::run_m5b_deconv_gpu(dj)) {
            throw std::runtime_error("run_m5b_deconv_gpu failed: " + dj.out_err);
        }
        require_clean("m5b_deconv");
    }
    // 4. Silicon cleanup (C13)
    {
        Stage45Fixture f(64, 64, 5);
        heraw_gpu::SiliconCleanupGpuJob cj;
        cj.H = 64; cj.W = 64; cj.raw = &f.frames;
        cj.fy = &f.fy; cj.fx = &f.fx; cj.mw = &f.mw;
        cj.cfa = &f.j.cfa6x6[0][0];
        cj.read_variance = 1e-6f; cj.shot = 2e-5f;
        mock_vk::st().reset_counters();
        if (!heraw_gpu::run_silicon_cleanup_gpu(cj)) {
            throw std::runtime_error("run_silicon_cleanup_gpu failed: " + cj.out_err);
        }
        require_clean("silicon_cleanup");
    }
    std::puts("other_vulkan_entry_points=PASS");
}

static void test_oom_unwind_exhaustively() {
    // Sweep every GpuBuf allocation point in V3 Stage 4/5 (including all 3 inner blocks)
    uint64_t v3_succeeded_at = 0;
    for (uint64_t fail_idx = 1; fail_idx <= 128; ++fail_idx) {
        Stage45Fixture f(128, 128, 3);
        f.j.math_v3_unified = true;
        f.j.is_1x_mode = false;
        f.j.defer_optical_psf = false;
        f.j.psf_iters = 2;
        mock_vk::st().reset_counters(fail_idx, 0);
        const bool ok = heraw_gpu::run_stage45_gpu(f.j);
        require_clean("v3_oom_unwind");
        if (ok) {
            v3_succeeded_at = fail_idx;
            break;
        }
    }
    if (v3_succeeded_at < 35) {
        throw std::runtime_error("Expected V3 OOM sweep to exercise inner-block allocations");
    }

    // Also verify device-local VRAM -> host-visible RAM fallback succeeds without leaks
    {
        Stage45Fixture f(128, 128, 3);
        f.j.math_v3_unified = true;
        mock_vk::st().reset_counters(0, 3);
        if (!heraw_gpu::run_stage45_gpu(f.j)) {
            throw std::runtime_error("VRAM->RAM fallback unexpectedly failed");
        }
        require_clean("vram_fallback");
    }

    // Sweep legacy Stage 4/5 OOM unwind as well
    uint64_t legacy_succeeded_at = 0;
    for (uint64_t fail_idx = 1; fail_idx <= 128; ++fail_idx) {
        Stage45Fixture f(128, 128, 3);
        f.j.math_v3_unified = false;
        f.j.psf_iters = 2;
        mock_vk::st().reset_counters(fail_idx, 0);
        const bool ok = heraw_gpu::run_stage45_gpu(f.j);
        require_clean("legacy_oom_unwind");
        if (ok) {
            legacy_succeeded_at = fail_idx;
            break;
        }
    }
    if (legacy_succeeded_at < 25) {
        throw std::runtime_error("Expected legacy OOM sweep to exercise inner-block allocations");
    }

    std::printf("oom_unwind v3_bufs=%llu legacy_bufs=%llu=PASS\n",
                static_cast<unsigned long long>(v3_succeeded_at - 1),
                static_cast<unsigned long long>(legacy_succeeded_at - 1));
}

int main() {
    heraw_gpu::detail::VulkanEngine::test_gipa_override = &mock_vk::vkGetInstanceProcAddr;
    try {
        const auto info = heraw_gpu::probe_gpu_device();
        if (!info.available) throw std::runtime_error("Mock Vulkan probe failed");
        test_v3_multitile_readback();
        test_legacy_multitile_readback();
        test_other_vulkan_entry_points();
        test_oom_unwind_exhaustively();
        std::puts("VULKAN_LIFETIME_PASS");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "VULKAN_LIFETIME_FAIL: %s\n", e.what());
        return 1;
    }
}
