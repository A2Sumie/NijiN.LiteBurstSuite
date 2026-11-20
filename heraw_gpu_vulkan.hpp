// heraw_gpu_vulkan.hpp -- Dynamic Zero-Dependency Vulkan Compute Backend (Stage 4 + Stage 5)
#pragma once

#if !defined(__APPLE__)

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#define VK_NO_PROTOTYPES 1
#include "third_party/vulkan/vulkan_core.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "heraw_cpu_cache.hpp"
#include "heraw_gpu_backend.hpp"
#include "heraw_gpu_spirv.hpp"

namespace heraw_gpu {

namespace detail {

enum KernelId {
    K_ZERO = 0,
    K_GREEN_ACCUM,
    K_GREEN_FINAL,
    K_CHROMA_ACCUM,
    K_CHROMA_FINAL,
    K_GAT_FWD,
    K_BOX_DOWN2X,
    K_DCT_WIENER,
    K_PYR_SUB_DOWN2X,
    K_PYR_LAP_ADD,
    K_EDGE_POLISH,
    K_DV_MAP,
    K_GAUSS_1D,
    K_GAT_INV_QUAD,
    K_MEDIAN3X3,
    K_GLINT_DESAT,
    K_ATROUS_1D,
    K_ASSEMBLE_RGB,
    K_RL_LUM,
    K_RL_PREP_H,
    K_RL_PREP_V,
    K_RL_RATIO,
    K_RL_UPDATE,
    K_RL_APPLY,
    K_SENSOR_RESIDUAL,
    K_V3_PREP_GUIDES,
    K_V3_GREEN,
    K_V3_CHROMA,
    K_V3_VST_FWD,
    K_V3_FFT64_SURE,
    K_V3_DV_HALF,
    K_V3_VST_INV,
    K_V3_IMPULSE_CLAMP,
    K_V3_DOWN2,
    K_V3_UP2_LAP,
    K_V3_ODD_HUBER,
    K_V3_ASSEMBLE_RGB,
    K_M5A_ZERO_U32,
    K_M5A_SCATTER,
    K_M5A_UPDATE,
    K_M5A_JACOBIAN,
    K_M5B_FFT64,
    K_M5B_CLAMP,
    K_C13_WARP_RGB,
    K_C13_SCENE,
    K_C13_MED_4PH,
    K_C13_HP97,
    K_C13_SENSOR_RES,
    K_C13_HOT_MAP,
    K_COUNT
};

struct SpvEntry {
    const uint32_t* code;
    size_t byte_size;
};

static const SpvEntry kSpvTable[K_COUNT] = {
    {heraw_spirv::k_zero_spv,            sizeof(heraw_spirv::k_zero_spv)},
    {heraw_spirv::k_green_accum_spv,     sizeof(heraw_spirv::k_green_accum_spv)},
    {heraw_spirv::k_green_final_spv,     sizeof(heraw_spirv::k_green_final_spv)},
    {heraw_spirv::k_chroma_accum_spv,    sizeof(heraw_spirv::k_chroma_accum_spv)},
    {heraw_spirv::k_chroma_final_spv,    sizeof(heraw_spirv::k_chroma_final_spv)},
    {heraw_spirv::k_gat_fwd_spv,         sizeof(heraw_spirv::k_gat_fwd_spv)},
    {heraw_spirv::k_box_down2x_spv,      sizeof(heraw_spirv::k_box_down2x_spv)},
    {heraw_spirv::k_dct_wiener_spv,      sizeof(heraw_spirv::k_dct_wiener_spv)},
    {heraw_spirv::k_pyr_sub_down2x_spv,  sizeof(heraw_spirv::k_pyr_sub_down2x_spv)},
    {heraw_spirv::k_pyr_lap_add_spv,     sizeof(heraw_spirv::k_pyr_lap_add_spv)},
    {heraw_spirv::k_edge_polish_spv,     sizeof(heraw_spirv::k_edge_polish_spv)},
    {heraw_spirv::k_dv_map_spv,          sizeof(heraw_spirv::k_dv_map_spv)},
    {heraw_spirv::k_gauss_1d_spv,        sizeof(heraw_spirv::k_gauss_1d_spv)},
    {heraw_spirv::k_gat_inv_quad_spv,    sizeof(heraw_spirv::k_gat_inv_quad_spv)},
    {heraw_spirv::k_median3x3_spv,       sizeof(heraw_spirv::k_median3x3_spv)},
    {heraw_spirv::k_glint_desat_spv,     sizeof(heraw_spirv::k_glint_desat_spv)},
    {heraw_spirv::k_atrous_1d_spv,       sizeof(heraw_spirv::k_atrous_1d_spv)},
    {heraw_spirv::k_assemble_rgb_spv,    sizeof(heraw_spirv::k_assemble_rgb_spv)},
    {heraw_spirv::k_rl_lum_spv,          sizeof(heraw_spirv::k_rl_lum_spv)},
    {heraw_spirv::k_rl_prep_h_spv,       sizeof(heraw_spirv::k_rl_prep_h_spv)},
    {heraw_spirv::k_rl_prep_v_spv,       sizeof(heraw_spirv::k_rl_prep_v_spv)},
    {heraw_spirv::k_rl_ratio_spv,        sizeof(heraw_spirv::k_rl_ratio_spv)},
    {heraw_spirv::k_rl_update_spv,       sizeof(heraw_spirv::k_rl_update_spv)},
    {heraw_spirv::k_rl_apply_spv,        sizeof(heraw_spirv::k_rl_apply_spv)},
    {heraw_spirv::k_sensor_residual_spv, sizeof(heraw_spirv::k_sensor_residual_spv)},
    {heraw_spirv::k_v3_prep_guides_spv,  sizeof(heraw_spirv::k_v3_prep_guides_spv)},
    {heraw_spirv::k_v3_green_spv,        sizeof(heraw_spirv::k_v3_green_spv)},
    {heraw_spirv::k_v3_chroma_spv,       sizeof(heraw_spirv::k_v3_chroma_spv)},
    {heraw_spirv::k_v3_vst_fwd_spv,      sizeof(heraw_spirv::k_v3_vst_fwd_spv)},
    {heraw_spirv::k_v3_fft64_sure_spv,   sizeof(heraw_spirv::k_v3_fft64_sure_spv)},
    {heraw_spirv::k_v3_dv_half_spv,      sizeof(heraw_spirv::k_v3_dv_half_spv)},
    {heraw_spirv::k_v3_vst_inv_spv,      sizeof(heraw_spirv::k_v3_vst_inv_spv)},
    {heraw_spirv::k_v3_impulse_clamp_spv,sizeof(heraw_spirv::k_v3_impulse_clamp_spv)},
    {heraw_spirv::k_v3_down2_spv,        sizeof(heraw_spirv::k_v3_down2_spv)},
    {heraw_spirv::k_v3_up2_lap_spv,      sizeof(heraw_spirv::k_v3_up2_lap_spv)},
    {heraw_spirv::k_v3_odd_huber_spv,    sizeof(heraw_spirv::k_v3_odd_huber_spv)},
    {heraw_spirv::k_v3_assemble_rgb_spv, sizeof(heraw_spirv::k_v3_assemble_rgb_spv)},
    {heraw_spirv::k_m5a_zero_u32_spv,    sizeof(heraw_spirv::k_m5a_zero_u32_spv)},
    {heraw_spirv::k_m5a_scatter_spv,     sizeof(heraw_spirv::k_m5a_scatter_spv)},
    {heraw_spirv::k_m5a_update_spv,      sizeof(heraw_spirv::k_m5a_update_spv)},
    {heraw_spirv::k_m5a_jacobian_spv,    sizeof(heraw_spirv::k_m5a_jacobian_spv)},
    {heraw_spirv::k_m5b_fft64_spv,       sizeof(heraw_spirv::k_m5b_fft64_spv)},
    {heraw_spirv::k_m5b_clamp_spv,       sizeof(heraw_spirv::k_m5b_clamp_spv)},
    {heraw_spirv::k_c13_warp_rgb_spv,    sizeof(heraw_spirv::k_c13_warp_rgb_spv)},
    {heraw_spirv::k_c13_scene_spv,       sizeof(heraw_spirv::k_c13_scene_spv)},
    {heraw_spirv::k_c13_med_4ph_spv,     sizeof(heraw_spirv::k_c13_med_4ph_spv)},
    {heraw_spirv::k_c13_hp97_spv,        sizeof(heraw_spirv::k_c13_hp97_spv)},
    {heraw_spirv::k_c13_sensor_res_spv,  sizeof(heraw_spirv::k_c13_sensor_res_spv)},
    {heraw_spirv::k_c13_hot_map_spv,     sizeof(heraw_spirv::k_c13_hot_map_spv)}
};

struct VulkanEngine {
    std::mutex mtx;
    bool inited = false;
    bool ok = false;
    bool is_discrete_gpu = false;
    bool auto_disable_recommended = false;
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    std::string dev_name;
    std::string init_err;
    uint32_t max_ssbo_range = 0;

#ifdef _WIN32
    HMODULE lib = nullptr;
#else
    void* lib = nullptr;
#endif

    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = nullptr;
    PFN_vkCreateInstance fpCreateInstance = nullptr;
    PFN_vkDestroyInstance fpDestroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices fpEnumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties fpGetPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties fpGetPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties fpGetPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkCreateDevice fpCreateDevice = nullptr;
    PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = nullptr;

    PFN_vkDestroyDevice fpDestroyDevice = nullptr;
    PFN_vkGetDeviceQueue fpGetDeviceQueue = nullptr;
    PFN_vkCreateBuffer fpCreateBuffer = nullptr;
    PFN_vkDestroyBuffer fpDestroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements fpGetBufferMemoryRequirements = nullptr;
    PFN_vkAllocateMemory fpAllocateMemory = nullptr;
    PFN_vkFreeMemory fpFreeMemory = nullptr;
    PFN_vkBindBufferMemory fpBindBufferMemory = nullptr;
    PFN_vkMapMemory fpMapMemory = nullptr;
    PFN_vkUnmapMemory fpUnmapMemory = nullptr;
    PFN_vkCreateShaderModule fpCreateShaderModule = nullptr;
    PFN_vkDestroyShaderModule fpDestroyShaderModule = nullptr;
    PFN_vkCreateDescriptorSetLayout fpCreateDescriptorSetLayout = nullptr;
    PFN_vkDestroyDescriptorSetLayout fpDestroyDescriptorSetLayout = nullptr;
    PFN_vkCreatePipelineLayout fpCreatePipelineLayout = nullptr;
    PFN_vkDestroyPipelineLayout fpDestroyPipelineLayout = nullptr;
    PFN_vkCreateComputePipelines fpCreateComputePipelines = nullptr;
    PFN_vkDestroyPipeline fpDestroyPipeline = nullptr;
    PFN_vkCreateDescriptorPool fpCreateDescriptorPool = nullptr;
    PFN_vkDestroyDescriptorPool fpDestroyDescriptorPool = nullptr;
    PFN_vkResetDescriptorPool fpResetDescriptorPool = nullptr;
    PFN_vkAllocateDescriptorSets fpAllocateDescriptorSets = nullptr;
    PFN_vkUpdateDescriptorSets fpUpdateDescriptorSets = nullptr;
    PFN_vkCreateCommandPool fpCreateCommandPool = nullptr;
    PFN_vkDestroyCommandPool fpDestroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers fpAllocateCommandBuffers = nullptr;
    PFN_vkResetCommandBuffer fpResetCommandBuffer = nullptr;
    PFN_vkBeginCommandBuffer fpBeginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer fpEndCommandBuffer = nullptr;
    PFN_vkCmdBindPipeline fpCmdBindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets fpCmdBindDescriptorSets = nullptr;
    PFN_vkCmdPushConstants fpCmdPushConstants = nullptr;
    PFN_vkCmdDispatch fpCmdDispatch = nullptr;
    PFN_vkCmdPipelineBarrier fpCmdPipelineBarrier = nullptr;
    PFN_vkCmdCopyBuffer fpCmdCopyBuffer = nullptr;
    PFN_vkCmdFillBuffer fpCmdFillBuffer = nullptr;
    PFN_vkCreateFence fpCreateFence = nullptr;
    PFN_vkDestroyFence fpDestroyFence = nullptr;
    PFN_vkResetFences fpResetFences = nullptr;
    PFN_vkWaitForFences fpWaitForFences = nullptr;
    PFN_vkQueueSubmit fpQueueSubmit = nullptr;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_props{};
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t q_fam = 0;

    VkDescriptorSetLayout ds_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
    VkPipeline pipes[K_COUNT] = {};
    VkDescriptorPool ds_pool = VK_NULL_HANDLE;
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    struct GpuBuf {
        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        void* mapped = nullptr;
    };

    uint32_t find_mem_type(uint32_t type_bits, VkMemoryPropertyFlags req) const {
        for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
            if ((type_bits & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & req) == req) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    bool alloc_buf(GpuBuf& out, VkDeviceSize bytes, bool host_visible) {
        out = GpuBuf{};
        out.size = std::max<VkDeviceSize>(64, (bytes + 15) & ~VkDeviceSize(15));
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = out.size;
        bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (fpCreateBuffer(dev, &bci, nullptr, &out.buf) != VK_SUCCESS) return false;

        VkMemoryRequirements mr{};
        fpGetBufferMemoryRequirements(dev, out.buf, &mr);

        uint32_t mt = UINT32_MAX;
        if (host_visible) {
            mt = find_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
            if (mt == UINT32_MAX)
                mt = find_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        } else {
            mt = find_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (mt == UINT32_MAX)
                mt = find_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        }
        if (mt == UINT32_MAX) {
            fpDestroyBuffer(dev, out.buf, nullptr);
            out.buf = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = mt;
        if (fpAllocateMemory(dev, &mai, nullptr, &out.mem) != VK_SUCCESS) {
            // Fallback to host-visible system RAM if device-local VRAM is full
            if (!host_visible) {
                mt = find_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                if (mt != UINT32_MAX) {
                    mai.memoryTypeIndex = mt;
                    if (fpAllocateMemory(dev, &mai, nullptr, &out.mem) != VK_SUCCESS) out.mem = VK_NULL_HANDLE;
                }
            }
            if (!out.mem) {
                fpDestroyBuffer(dev, out.buf, nullptr);
                out.buf = VK_NULL_HANDLE;
                return false;
            }
        }
        if (fpBindBufferMemory(dev, out.buf, out.mem, 0) != VK_SUCCESS) {
            free_buf(out);
            return false;
        }
        if (host_visible) {
            if (fpMapMemory(dev, out.mem, 0, out.size, 0, &out.mapped) != VK_SUCCESS) {
                free_buf(out);
                return false;
            }
        }
        return true;
    }

    void free_buf(GpuBuf& b) {
        if (b.mapped && dev) { fpUnmapMemory(dev, b.mem); b.mapped = nullptr; }
        if (b.buf && dev) { fpDestroyBuffer(dev, b.buf, nullptr); b.buf = VK_NULL_HANDLE; }
        if (b.mem && dev) { fpFreeMemory(dev, b.mem, nullptr); b.mem = VK_NULL_HANDLE; }
        b.size = 0;
    }

    bool ensure_init() {
        std::lock_guard<std::mutex> lk(mtx);
        if (inited) return ok;
        inited = true;

#ifdef _WIN32
        lib = LoadLibraryW(L"vulkan-1.dll");
        if (!lib) { init_err = "vulkan-1.dll not found"; return false; }
        fpGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            reinterpret_cast<void*>(GetProcAddress(lib, "vkGetInstanceProcAddr")));
#else
        lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!lib) lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) { init_err = "libvulkan.so not found"; return false; }
        fpGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));
#endif
        if (!fpGetInstanceProcAddr) { init_err = "vkGetInstanceProcAddr missing"; return false; }

        fpCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(fpGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
        if (!fpCreateInstance) { init_err = "vkCreateInstance missing"; return false; }

        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "nijinLBS";
        app.applicationVersion = VK_MAKE_VERSION(1, 5, 0);
        app.pEngineName = "nijinLBS";
        app.engineVersion = VK_MAKE_VERSION(1, 5, 0);
        app.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ici.pApplicationInfo = &app;
        if (fpCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS || !instance) {
            init_err = "vkCreateInstance failed";
            return false;
        }

        #define LOAD_INST(fn) fp##fn = reinterpret_cast<PFN_vk##fn>(reinterpret_cast<void*>(fpGetInstanceProcAddr(instance, "vk" #fn))); if (!fp##fn) { init_err = "Missing vk" #fn; return false; }
        LOAD_INST(DestroyInstance)
        LOAD_INST(EnumeratePhysicalDevices)
        LOAD_INST(GetPhysicalDeviceProperties)
        LOAD_INST(GetPhysicalDeviceMemoryProperties)
        LOAD_INST(GetPhysicalDeviceQueueFamilyProperties)
        LOAD_INST(CreateDevice)
        LOAD_INST(GetDeviceProcAddr)
        #undef LOAD_INST

        uint32_t ndev = 0;
        if (fpEnumeratePhysicalDevices(instance, &ndev, nullptr) != VK_SUCCESS || ndev == 0) {
            init_err = "No Vulkan physical devices";
            return false;
        }
        std::vector<VkPhysicalDevice> devs(ndev);
        fpEnumeratePhysicalDevices(instance, &ndev, devs.data());

        int best_score = -1;
        for (uint32_t d = 0; d < ndev; ++d) {
            VkPhysicalDeviceProperties props{};
            fpGetPhysicalDeviceProperties(devs[d], &props);
            uint32_t nqf = 0;
            fpGetPhysicalDeviceQueueFamilyProperties(devs[d], &nqf, nullptr);
            std::vector<VkQueueFamilyProperties> qfs(nqf);
            fpGetPhysicalDeviceQueueFamilyProperties(devs[d], &nqf, qfs.data());
            uint32_t found_q = UINT32_MAX;
            for (uint32_t q = 0; q < nqf; ++q) {
                if (qfs[q].queueFlags & VK_QUEUE_COMPUTE_BIT) { found_q = q; break; }
            }
            if (found_q == UINT32_MAX) continue;
            if (props.limits.maxComputeSharedMemorySize < 20480) continue;

            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
                const char* allow_sw = std::getenv("NIJIN_ALLOW_SOFTWARE_VULKAN");
                if (!allow_sw || std::strcmp(allow_sw, "1") != 0) continue;
            }
            VkPhysicalDeviceMemoryProperties cand_mp{};
            fpGetPhysicalDeviceMemoryProperties(devs[d], &cand_mp);
            uint64_t vram_mb = 0;
            for (uint32_t h = 0; h < cand_mp.memoryHeapCount; ++h) {
                if (cand_mp.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                    vram_mb = std::max<uint64_t>(vram_mb, uint64_t(cand_mp.memoryHeaps[h].size) >> 20);
                }
            }
            const bool disc = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
            const bool weak_igpu = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) &&
                heraw_cache::should_default_disable_igpu(false, props.vendorID, props.deviceID, props.deviceName);

            int score = 500;
            if (disc) {
                // Desktop discrete GPUs strictly outrank integrated GPUs regardless of adapter enumeration order
                score = 1000000 + int(std::min<uint64_t>(vram_mb, 500000ULL));
            } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                score = (weak_igpu ? 1000 : 20000) + int(std::min<uint64_t>(vram_mb, 8000ULL));
            } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
                score = 10;
            }

            if (score > best_score) {
                best_score = score;
                phys = devs[d];
                q_fam = found_q;
                is_discrete_gpu = disc;
                auto_disable_recommended = weak_igpu;
                vendor_id = props.vendorID;
                device_id = props.deviceID;
                dev_name = props.deviceName;
                max_ssbo_range = props.limits.maxStorageBufferRange;
            }
        }
        if (!phys) { init_err = "No compute-capable Vulkan GPU found"; return false; }
        fpGetPhysicalDeviceMemoryProperties(phys, &mem_props);

        float q_prio = 1.0f;
        VkDeviceQueueCreateInfo dqci{};
        dqci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        dqci.queueFamilyIndex = q_fam;
        dqci.queueCount = 1;
        dqci.pQueuePriorities = &q_prio;

        VkDeviceCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &dqci;
        if (fpCreateDevice(phys, &dci, nullptr, &dev) != VK_SUCCESS || !dev) {
            init_err = "vkCreateDevice failed";
            return false;
        }

        #define LOAD_DEV(fn) fp##fn = reinterpret_cast<PFN_vk##fn>(reinterpret_cast<void*>(fpGetDeviceProcAddr(dev, "vk" #fn))); if (!fp##fn) { init_err = "Missing vk" #fn; return false; }
        LOAD_DEV(DestroyDevice)
        LOAD_DEV(GetDeviceQueue)
        LOAD_DEV(CreateBuffer)
        LOAD_DEV(DestroyBuffer)
        LOAD_DEV(GetBufferMemoryRequirements)
        LOAD_DEV(AllocateMemory)
        LOAD_DEV(FreeMemory)
        LOAD_DEV(BindBufferMemory)
        LOAD_DEV(MapMemory)
        LOAD_DEV(UnmapMemory)
        LOAD_DEV(CreateShaderModule)
        LOAD_DEV(DestroyShaderModule)
        LOAD_DEV(CreateDescriptorSetLayout)
        LOAD_DEV(DestroyDescriptorSetLayout)
        LOAD_DEV(CreatePipelineLayout)
        LOAD_DEV(DestroyPipelineLayout)
        LOAD_DEV(CreateComputePipelines)
        LOAD_DEV(DestroyPipeline)
        LOAD_DEV(CreateDescriptorPool)
        LOAD_DEV(DestroyDescriptorPool)
        LOAD_DEV(ResetDescriptorPool)
        LOAD_DEV(AllocateDescriptorSets)
        LOAD_DEV(UpdateDescriptorSets)
        LOAD_DEV(CreateCommandPool)
        LOAD_DEV(DestroyCommandPool)
        LOAD_DEV(AllocateCommandBuffers)
        LOAD_DEV(ResetCommandBuffer)
        LOAD_DEV(BeginCommandBuffer)
        LOAD_DEV(EndCommandBuffer)
        LOAD_DEV(CmdBindPipeline)
        LOAD_DEV(CmdBindDescriptorSets)
        LOAD_DEV(CmdPushConstants)
        LOAD_DEV(CmdDispatch)
        LOAD_DEV(CmdPipelineBarrier)
        LOAD_DEV(CmdCopyBuffer)
        LOAD_DEV(CmdFillBuffer)
        LOAD_DEV(CreateFence)
        LOAD_DEV(DestroyFence)
        LOAD_DEV(ResetFences)
        LOAD_DEV(WaitForFences)
        LOAD_DEV(QueueSubmit)
        #undef LOAD_DEV

        fpGetDeviceQueue(dev, q_fam, 0, &queue);

        // Universal 6-SSBO Descriptor Set Layout + 128-byte Push Constant Pipeline Layout
        VkDescriptorSetLayoutBinding binds[6] = {};
        for (uint32_t i = 0; i < 6; ++i) {
            binds[i].binding = i;
            binds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binds[i].descriptorCount = 1;
            binds[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 6;
        dslci.pBindings = binds;
        if (fpCreateDescriptorSetLayout(dev, &dslci, nullptr, &ds_layout) != VK_SUCCESS) {
            init_err = "vkCreateDescriptorSetLayout failed";
            return false;
        }

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcr.offset = 0;
        pcr.size = 128;

        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &ds_layout;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcr;
        if (fpCreatePipelineLayout(dev, &plci, nullptr, &pipe_layout) != VK_SUCCESS) {
            init_err = "vkCreatePipelineLayout failed";
            return false;
        }

        for (int i = 0; i < K_COUNT; ++i) {
            VkShaderModuleCreateInfo smci{};
            smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            smci.codeSize = kSpvTable[i].byte_size;
            smci.pCode = kSpvTable[i].code;
            VkShaderModule mod = VK_NULL_HANDLE;
            if (fpCreateShaderModule(dev, &smci, nullptr, &mod) != VK_SUCCESS) {
                init_err = "vkCreateShaderModule failed on kernel " + std::to_string(i);
                return false;
            }
            VkComputePipelineCreateInfo cpci{};
            cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpci.stage.module = mod;
            cpci.stage.pName = "main";
            cpci.layout = pipe_layout;
            VkResult prc = fpCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipes[i]);
            fpDestroyShaderModule(dev, mod, nullptr);
            if (prc != VK_SUCCESS) {
                init_err = "vkCreateComputePipelines failed on kernel " + std::to_string(i);
                return false;
            }
        }

        VkDescriptorPoolSize psz{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256 * 6};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 256;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &psz;
        if (fpCreateDescriptorPool(dev, &dpci, nullptr, &ds_pool) != VK_SUCCESS) {
            init_err = "vkCreateDescriptorPool failed";
            return false;
        }

        VkCommandPoolCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpci.queueFamilyIndex = q_fam;
        if (fpCreateCommandPool(dev, &cpci, nullptr, &cmd_pool) != VK_SUCCESS) {
            init_err = "vkCreateCommandPool failed";
            return false;
        }

        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = cmd_pool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        if (fpAllocateCommandBuffers(dev, &cbai, &cmd) != VK_SUCCESS) {
            init_err = "vkAllocateCommandBuffers failed";
            return false;
        }

        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (fpCreateFence(dev, &fci, nullptr, &fence) != VK_SUCCESS) {
            init_err = "vkCreateFence failed";
            return false;
        }

        ok = true;
        return true;
    }
};

static VulkanEngine& global_vulkan() {
    static VulkanEngine eng;
    return eng;
}

} // namespace detail

inline GpuDeviceInfo probe_gpu_device() {
    using namespace detail;
    GpuDeviceInfo info;
    auto& vk = global_vulkan();
    if (vk.ensure_init()) {
        info.available = true;
        info.api_name = "Vulkan";
        info.device_name = vk.dev_name;
    }
    return info;
}

inline bool should_auto_disable_gpu(std::string* out_dev_name = nullptr) {
    if (const char* env = std::getenv("NIJIN_GPU_POLICY_TEST")) {
        if (std::strcmp(env, "apu") == 0) {
            if (out_dev_name) *out_dev_name = "AMD Radeon 880M Graphics";
            return true;
        }
        if (std::strcmp(env, "intel_old") == 0) {
            if (out_dev_name) *out_dev_name = "Intel(R) Iris(R) Plus Graphics 655";
            return true;
        }
        if (std::strcmp(env, "intel_xe2") == 0 || std::strcmp(env, "dgpu") == 0) {
            return false;
        }
    }
    using namespace detail;
    auto& vk = global_vulkan();
    if (!vk.ensure_init()) return false;
    if (vk.auto_disable_recommended) {
        if (out_dev_name) *out_dev_name = vk.dev_name;
        return true;
    }
    return false;
}

inline bool run_stage45_gpu_full(Stage45GpuJob& job) {
    auto cost_start=std::chrono::steady_clock::now();
    job.out_wall_seconds.fill(0);job.out_device_seconds.fill(0);job.out_has_device_timing=false;
    auto cost_mark=[&](size_t phase) {
        const auto now=std::chrono::steady_clock::now();
        job.out_wall_seconds[phase]+=std::chrono::duration<double>(now-cost_start).count();
        cost_start=now;
    };
    using namespace detail;
    auto& vk = global_vulkan();
    if (!vk.ensure_init()) {
        job.out_err = vk.init_err;
        return false;
    }

    const int H1 = job.H1, W1 = job.W1;
    const int Hh = H1 / 2, Wh = W1 / 2;
    const int H2 = H1 * 2, W2 = W1 * 2;
    const int Eh = (H1 + 3) / 4, Ew = (W1 + 3) / 4;
    const size_t n_half = (size_t)Hh * Wh;
    const size_t n_1x   = (size_t)H1 * W1;
    const size_t n_2x   = (size_t)H2 * W2;
    const size_t n_eta  = (size_t)Eh * Ew;

    const VkDeviceSize plane2x_bytes = (VkDeviceSize)n_2x * sizeof(float);
    if (vk.max_ssbo_range < plane2x_bytes) {
        job.out_err = "Vulkan maxStorageBufferRange smaller than 2x plane";
        return false;
    }

    auto t_s4 = std::chrono::steady_clock::now();
    job.out_device_label = "Vulkan (" + vk.dev_name + ")";

    // Scope guard freeing all allocated GpuBufs automatically
    std::vector<VulkanEngine::GpuBuf*> owned;
    struct Cleanup {
        VulkanEngine& vk;
        std::vector<VulkanEngine::GpuBuf*>& list;
        ~Cleanup() { for (auto* p : list) if (p) vk.free_buf(*p); }
    } cleanup{vk, owned};

    auto make_buf = [&](VulkanEngine::GpuBuf& b, VkDeviceSize sz, bool host_vis) -> bool {
        if (!vk.alloc_buf(b, sz, host_vis)) return false;
        owned.push_back(&b);
        return true;
    };

    VulkanEngine::GpuBuf b_dummy, b_stage;
    VulkanEngine::GpuBuf b_frame, b_flow_mw, b_icov, b_canc, b_g0ha, b_boost, b_rlmod;
    VulkanEngine::GpuBuf b_plane0, b_plane1, b_plane2, b_eta, b_dR, b_dB;

    const VkDeviceSize stage_bytes = std::max<VkDeviceSize>(plane2x_bytes,
        (VkDeviceSize)(n_half * 6 + n_1x * 2) * sizeof(float));
    if (!make_buf(b_dummy, 64, false) ||
        !make_buf(b_stage, stage_bytes, true) ||
        !make_buf(b_frame, n_1x * sizeof(float), false) ||
        !make_buf(b_flow_mw, n_half * sizeof(float) * 4, false) ||
        !make_buf(b_icov, n_half * sizeof(float) * 4, false) ||
        !make_buf(b_canc, n_1x * sizeof(float), false) ||
        !make_buf(b_g0ha, n_1x * sizeof(float), false) ||
        !make_buf(b_boost, n_half * sizeof(float), false) ||
        !make_buf(b_rlmod, n_half * sizeof(uint32_t), false) ||
        !make_buf(b_plane0, plane2x_bytes, false) ||
        !make_buf(b_plane1, plane2x_bytes, false) ||
        !make_buf(b_plane2, plane2x_bytes, false) ||
        !make_buf(b_eta, n_eta * sizeof(float), false) ||
        !make_buf(b_dR, n_1x * sizeof(float), false) ||
        !make_buf(b_dB, n_1x * sizeof(float), false)) {
        job.out_err = "Vulkan buffer allocation failed";
        return false;
    }

    auto begin_cb = [&]() {
        vk.fpResetDescriptorPool(vk.dev, vk.ds_pool, 0);
        vk.fpResetCommandBuffer(vk.cmd, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk.fpBeginCommandBuffer(vk.cmd, &bi);
    };
    auto submit_wait = [&]() -> bool {
        if (vk.fpEndCommandBuffer(vk.cmd) != VK_SUCCESS) return false;
        vk.fpResetFences(vk.dev, 1, &vk.fence);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &vk.cmd;
        if (vk.fpQueueSubmit(vk.queue, 1, &si, vk.fence) != VK_SUCCESS) return false;
        return vk.fpWaitForFences(vk.dev, 1, &vk.fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    };
    auto mem_barrier = [&]() {
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
        vk.fpCmdPipelineBarrier(vk.cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &mb, 0, nullptr, 0, nullptr);
    };
    auto bind_and_dispatch = [&](KernelId kid, std::initializer_list<const VulkanEngine::GpuBuf*> bufs,
                                 const void* pc, uint32_t pc_sz, uint32_t gx, uint32_t gy) {
        VkDescriptorSet ds = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = vk.ds_pool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &vk.ds_layout;
        vk.fpAllocateDescriptorSets(vk.dev, &dsai, &ds);

        VkDescriptorBufferInfo dbi[6] = {};
        VkWriteDescriptorSet wds[6] = {};
        int idx = 0;
        for (const auto* b : bufs) {
            if (idx >= 6) break;
            dbi[idx] = {b->buf, 0, VK_WHOLE_SIZE};
            idx++;
        }
        for (; idx < 6; ++idx) {
            dbi[idx] = {b_dummy.buf, 0, VK_WHOLE_SIZE};
        }
        for (int i = 0; i < 6; ++i) {
            wds[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wds[i].dstSet = ds;
            wds[i].dstBinding = (uint32_t)i;
            wds[i].descriptorCount = 1;
            wds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            wds[i].pBufferInfo = &dbi[i];
        }
        vk.fpUpdateDescriptorSets(vk.dev, 6, wds, 0, nullptr);
        vk.fpCmdBindPipeline(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pipes[kid]);
        vk.fpCmdBindDescriptorSets(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pipe_layout, 0, 1, &ds, 0, nullptr);
        if (pc && pc_sz > 0) {
            vk.fpCmdPushConstants(vk.cmd, vk.pipe_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pc_sz, pc);
        }
        vk.fpCmdDispatch(vk.cmd, gx, gy, 1);
        mem_barrier();
    };
    auto disp_1d = [&](KernelId kid, std::initializer_list<const VulkanEngine::GpuBuf*> bufs,
                       const void* pc, uint32_t pc_sz, uint32_t count) {
        bind_and_dispatch(kid, bufs, pc, pc_sz, (count + 255) / 256, 1);
    };
    auto disp_2d = [&](KernelId kid, std::initializer_list<const VulkanEngine::GpuBuf*> bufs,
                       const void* pc, uint32_t pc_sz, int W, int H) {
        bind_and_dispatch(kid, bufs, pc, pc_sz, (uint32_t)((W + 15) / 16), (uint32_t)((H + 15) / 16));
    };

    // Upload static Stage 4 guides (icov_guide, c_anc_1x, snr_boost) + zero accumulators
    {
        uint8_t* st = (uint8_t*)b_stage.mapped;
        VkDeviceSize off_icov = 0;
        VkDeviceSize sz_icov  = n_half * sizeof(float) * 4;
        VkDeviceSize off_canc = sz_icov;
        VkDeviceSize sz_canc  = n_1x * sizeof(float);
        VkDeviceSize off_g0ha = off_canc + sz_canc;
        VkDeviceSize sz_g0ha  = n_1x * sizeof(float);
        VkDeviceSize off_bst  = off_g0ha + sz_g0ha;
        VkDeviceSize sz_bst   = n_half * sizeof(float);
        VkDeviceSize off_rlm  = off_bst + sz_bst;
        VkDeviceSize sz_rlm   = n_half * sizeof(uint32_t);

        float* dst_ic = (float*)(st + off_icov);
        for (size_t i = 0; i < n_half; ++i) {
            float axx = job.inv_cov[i * 3 + 0];
            float axy = job.inv_cov[i * 3 + 1];
            float ayy = job.inv_cov[i * 3 + 2];
            if (job.tile_spec_map) {
                eval_tile_modulated_icov(job, int(i / size_t(std::max(1, Wh))), int(i % size_t(std::max(1, Wh))), axx, axy, ayy);
            }
            dst_ic[i * 4 + 0] = axx;
            dst_ic[i * 4 + 1] = axy;
            dst_ic[i * 4 + 2] = ayy;
            dst_ic[i * 4 + 3] = job.g_guide[i];
        }
        std::memcpy(st + off_canc, job.c_anc_1x, sz_canc);
        std::memcpy(st + off_g0ha, job.g0_ha_1x, sz_g0ha);
        std::memcpy(st + off_bst, job.snr_boost_half, sz_bst);
        std::memcpy(st + off_rlm, job.rl_mod_f16, sz_rlm);

        begin_cb();
        VkBufferCopy c1{off_icov, 0, sz_icov}, c2{off_canc, 0, sz_canc}, c3{off_g0ha, 0, sz_g0ha};
        VkBufferCopy c4{off_bst, 0, sz_bst}, c5{off_rlm, 0, sz_rlm};
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_icov.buf, 1, &c1);
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_canc.buf, 1, &c2);
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_g0ha.buf, 1, &c3);
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_boost.buf, 1, &c4);
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_rlmod.buf, 1, &c5);
        vk.fpCmdFillBuffer(vk.cmd, b_plane1.buf, 0, VK_WHOLE_SIZE, 0);
        vk.fpCmdFillBuffer(vk.cmd, b_plane2.buf, 0, VK_WHOLE_SIZE, 0);
        vk.fpCmdFillBuffer(vk.cmd, b_eta.buf,    0, VK_WHOLE_SIZE, 0);
        mem_barrier();
        if (!submit_wait()) { job.out_err = "Vulkan static upload failed"; return false; }
    }

    // --- Stage 4A: 2x Quincunx Green Accumulation ---
    cost_mark(0);
    uint32_t cfa_w[3] = {0, 0, 0};
    pack_cfa6x6_bits(job.cfa6x6, cfa_w);

    auto stage_frame_parallel = [&](int n, float mw_scale) {
        uint8_t* st = (uint8_t*)b_stage.mapped;
        const VkDeviceSize sz_fr = n_1x * sizeof(float);
        const float* src_f = (*job.frames_lin)[n].data();
        float* dst_f = (float*)st;
        float* fmw = (float*)(st + sz_fr);
        const float* fy = (*job.flows_fy)[n].data();
        const float* fx = (*job.flows_fx)[n].data();
        const float* mw = (*job.mw_half)[n].data();
        const int p_nt = (n_half >= 65536 && job.n_threads > 1) ? std::min(job.n_threads, 8) : 1;
        if (p_nt <= 1) {
            std::memcpy(dst_f, src_f, sz_fr);
            for (size_t i = 0; i < n_half; ++i) {
                fmw[i * 4 + 0] = fy[i];
                fmw[i * 4 + 1] = fx[i];
                fmw[i * 4 + 2] = gpu_motion_weight(job, mw[i]) * mw_scale;
                fmw[i * 4 + 3] = 0.0f;
            }
        } else {
            std::vector<heraw_sync::JoiningThread> workers;
            workers.reserve(p_nt);
            const size_t ch_1x = (n_1x + p_nt - 1) / p_nt;
            const size_t ch_h  = (n_half + p_nt - 1) / p_nt;
            for (int t = 0; t < p_nt; ++t) {
                workers.emplace_back([=, &job]() {
                    const size_t a0 = size_t(t) * ch_1x, a1 = std::min(n_1x, a0 + ch_1x);
                    if (a0 < a1) std::memcpy(dst_f + a0, src_f + a0, (a1 - a0) * sizeof(float));
                    const size_t h0 = size_t(t) * ch_h, h1 = std::min(n_half, h0 + ch_h);
                    for (size_t i = h0; i < h1; ++i) {
                        fmw[i * 4 + 0] = fy[i];
                        fmw[i * 4 + 1] = fx[i];
                        fmw[i * 4 + 2] = gpu_motion_weight(job, mw[i]) * mw_scale;
                        fmw[i * 4 + 3] = 0.0f;
                    }
                });
            }
        }
    };

    for (int n = 0; n < job.N; ++n) {
        VkDeviceSize sz_fr = n_1x * sizeof(float);
        VkDeviceSize sz_fm = n_half * sizeof(float) * 4;
        stage_frame_parallel(n, 1.0f);

        begin_cb();
        VkBufferCopy cf{0, 0, sz_fr}, cm{sz_fr, 0, sz_fm};
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_frame.buf, 1, &cf);
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_flow_mw.buf, 1, &cm);
        mem_barrier();

        float fw_s = gpu_frame_weight(job, n);
        PC_GreenAccum pc{H1, W1, Hh, Wh, Ew, (n == 0 ? 1 : 0), gpu_use_robust(job, n) ? 1 : 0,
                         job.is_xtrans ? 1 : 0, cfa_w[0], cfa_w[1], cfa_w[2],
                         (float)job.pg_sig0_sq, (float)job.pg_gain, fw_s, job.origin_y, job.origin_x,
                         job.q_luma_var, job.q_nlt_s0, job.q_nlt_s1, job.q_nlt_s2};
        disp_2d(K_GREEN_ACCUM, {&b_frame, &b_flow_mw, &b_icov, &b_plane1, &b_plane2, &b_eta}, &pc, sizeof(pc), W1, H1);
        if (n == job.N - 1) {
            PC_GreenFinal pcf{H1, W1, Hh, Wh, Ew};
            disp_2d(K_GREEN_FINAL, {&b_plane1, &b_plane2, &b_eta, &b_icov, &b_g0ha, &b_plane0}, &pcf, sizeof(pcf), W1, H1);
            if (job.global_eta < 0.0 || job.eta_quarter_out) {
                VkBufferCopy ce{0, 0, n_eta * sizeof(float)};
                vk.fpCmdCopyBuffer(vk.cmd, b_eta.buf, b_stage.buf, 1, &ce);
                mem_barrier();
            }
        }
        if (!submit_wait()) { job.out_err = "Vulkan green accum failed"; return false; }
    }

    if (job.global_eta < 0.0 || job.eta_quarter_out)
        gpu_eta_stats(job, (const float*)b_stage.mapped, Ew, Eh);
    const double eta_eff = job.global_eta >= 0.0 ? job.global_eta :
        std::max(0.01, job.out_eta_sum / double(std::max<int64_t>(1, job.out_eta_count)));
    job.out_eta_eff = eta_eff;
    cost_mark(1);
    if (job.eta_only) return true;
    vk.free_buf(b_eta);
    vk.free_buf(b_g0ha);

    // --- Stage 4B: Chroma (dR_1x, dB_1x) Accumulation ---
    for (int n = 0; n < job.N; ++n) {
        VkDeviceSize sz_fr = n_1x * sizeof(float);
        VkDeviceSize sz_fm = n_half * sizeof(float) * 4;
        stage_frame_parallel(n, gpu_frame_weight(job, n));

        float odry = 0.0f, odrx = 0.0f, odby = 0.0f, odbx = 0.0f;
        if (job.frame_ois_shifts && (size_t)(n * 4 + 3) < job.frame_ois_shifts->size()) {
            odry = (*job.frame_ois_shifts)[n * 4 + 0];
            odrx = (*job.frame_ois_shifts)[n * 4 + 1];
            odby = (*job.frame_ois_shifts)[n * 4 + 2];
            odbx = (*job.frame_ois_shifts)[n * 4 + 3];
        }

        begin_cb();
        if (n == 0) {
            vk.fpCmdFillBuffer(vk.cmd, b_plane1.buf, 0, n_1x * sizeof(float) * 4, 0);
        }
        VkBufferCopy cf{0, 0, sz_fr}, cm{sz_fr, 0, sz_fm};
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_frame.buf, 1, &cf);
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_flow_mw.buf, 1, &cm);
        mem_barrier();

        PC_ChromaAccum pc{H1, W1, Hh, Wh, (n == 0 ? 1 : 0), gpu_use_robust(job, n) ? 1 : 0,
                          job.is_xtrans ? 1 : 0, cfa_w[0], cfa_w[1], cfa_w[2],
                          (float)job.pg_sig0_sq, (float)job.pg_gain, job.wb_r, job.wb_b,
                          job.lca_r_k1, job.lca_r_k2, job.lca_b_k1, job.lca_b_k2,
                          odry, odrx, odby, odbx, job.full_H1, job.full_W1, job.origin_y, job.origin_x,
                          job.q_chroma_var, job.q_nlt_s0, job.q_nlt_s1, job.q_nlt_s2};
        disp_2d(K_CHROMA_ACCUM, {&b_frame, &b_flow_mw, &b_icov, &b_plane0, &b_canc, &b_plane1}, &pc, sizeof(pc), W1, H1);
        if (n == job.N - 1) {
            const float hl_start = job.glint_desat ? (job.is_astro ? 0.88f : 0.85f) : 1.0e6f;
            PC_ChromaFinal pcf{H1, W1, Hh, Wh, job.wb_r, job.wb_b,
                               hl_start, 0.14f,
                               cfa_w[0], cfa_w[1], cfa_w[2], job.is_xtrans ? 1 : 0};
            disp_2d(K_CHROMA_FINAL, {&b_plane1, &b_plane0, &b_canc, &b_dR, &b_dB}, &pcf, sizeof(pcf), W1, H1);
        }
        if (!submit_wait()) { job.out_err = "Vulkan chroma accum failed"; return false; }
    }
    vk.free_buf(b_frame);
    vk.free_buf(b_flow_mw);
    vk.free_buf(b_icov);
    vk.free_buf(b_canc);

    double dt_s4 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_s4).count();
    if (job.on_stage4_done) job.on_stage4_done(eta_eff, dt_s4, job.out_device_label);

    // --- Stage 5A: 5-Level Multiscale 8x8 DCT Wiener ---
    cost_mark(2);
    double sig_eff_sq = 0.0, g_eff = 0.0;
    gpu_stage5_gat_params(job, eta_eff, sig_eff_sq, g_eff);
    const double ped = 4.0 * std::sqrt(sig_eff_sq);
    const double gat_c = 0.375 * g_eff * g_eff + sig_eff_sq;
    const double sqrt_c = std::sqrt(gat_c);

    VulkanEngine::GpuBuf b_den0, b_pilot, b_pyr_s[5], b_den_s[5], b_dv1, b_dv2, b_gquad;
    if (!make_buf(b_den0, plane2x_bytes, false) ||
        !make_buf(b_pilot, plane2x_bytes, false) ||
        !make_buf(b_dv1, n_half * sizeof(float), false) ||
        !make_buf(b_dv2, n_half * sizeof(float), false) ||
        !make_buf(b_gquad, n_1x * sizeof(float), false)) {
        job.out_err = "Vulkan DCT buffer allocation failed";
        return false;
    }

    int Hs[5], Ws[5];
    Hs[0] = H2; Ws[0] = W2;
    const VulkanEngine::GpuBuf* pyr_ptr[5] = {};
    const VulkanEngine::GpuBuf* den_ptr[5] = {};
    pyr_ptr[0] = &b_plane2;
    den_ptr[0] = &b_den0;
    for (int s = 1; s < 5; ++s) {
        Hs[s] = Hs[s - 1] / 2;
        Ws[s] = Ws[s - 1] / 2;
        if (!make_buf(b_pyr_s[s], (VkDeviceSize)Hs[s] * Ws[s] * sizeof(float), false) ||
            !make_buf(b_den_s[s], (VkDeviceSize)Hs[s] * Ws[s] * sizeof(float), false)) {
            job.out_err = "Vulkan pyramid allocation failed";
            return false;
        }
        pyr_ptr[s] = &b_pyr_s[s];
        den_ptr[s] = &b_den_s[s];
    }

    {
        begin_cb();
        PC_GatFwd pc_gf{(uint32_t)n_2x, (float)ped, (float)g_eff, (float)gat_c, (float)sqrt_c};
        disp_1d(K_GAT_FWD, {&b_plane0, &b_plane1, pyr_ptr[0]}, &pc_gf, sizeof(pc_gf), (uint32_t)n_2x);

        if (job.denoise_strength > 1e-4f) {
            const float base_sig[5] = {1.35f, 1.15f, 0.78f, 0.48f, 0.28f};
            for (int s = 1; s < 5; ++s) {
                PC_BoxDown2x pcd{Hs[s - 1], Ws[s - 1], Hs[s], Ws[s]};
                disp_2d(K_BOX_DOWN2X, {pyr_ptr[s - 1], pyr_ptr[s]}, &pcd, sizeof(pcd), Ws[s], Hs[s]);
            }
            for (int s = 4; s >= 0; --s) {
                float sig_s = base_sig[s] * job.denoise_strength * job.dct_scale_mul[s];
                uint32_t tx = (uint32_t)((Ws[s] + 19) / 20);
                uint32_t ty = (uint32_t)((Hs[s] + 9) / 10);

                PC_DctWiener pcp{Hs[s], Ws[s], Hh, Wh, 0, (1 << s), sig_s};
                dct_dependency_roi(pcp,job);
                bind_and_dispatch(K_DCT_WIENER, {pyr_ptr[s], pyr_ptr[s], &b_boost, &b_pilot}, &pcp, sizeof(pcp), tx, ty);

                PC_DctWiener pcw{Hs[s], Ws[s], Hh, Wh, 1, (1 << s), sig_s};
                dct_dependency_roi(pcw,job);
                bind_and_dispatch(K_DCT_WIENER, {pyr_ptr[s], &b_pilot, &b_boost, den_ptr[s]}, &pcw, sizeof(pcw), tx, ty);

                if (s < 4) {
                    PC_BoxDown2x pcs{Hs[s], Ws[s], Hs[s + 1], Ws[s + 1]};
                    disp_2d(K_PYR_SUB_DOWN2X, {den_ptr[s], den_ptr[s + 1], &b_pilot}, &pcs, sizeof(pcs), Ws[s + 1], Hs[s + 1]);
                    float tau = std::max(1e-4f, 3.4f * sig_s);
                    PC_PyrLapAdd pcl{Hs[s], Ws[s], Hs[s + 1], Ws[s + 1], tau * tau};
                    disp_2d(K_PYR_LAP_ADD, {&b_pilot, den_ptr[s]}, &pcl, sizeof(pcl), Ws[s], Hs[s]);
                }
            }
            if (job.edge_polish) {
                PC_EdgePolish pce0{H2, W2, 0}, pce1{H2, W2, 1};
                disp_2d(K_EDGE_POLISH, {den_ptr[0], &b_pilot}, &pce0, sizeof(pce0), W2, H2);
                disp_2d(K_EDGE_POLISH, {&b_pilot, den_ptr[0]}, &pce1, sizeof(pce1), W2, H2);
            }
        } else {
            den_ptr[0] = pyr_ptr[0];
        }

        PC_DvMap pcdv{Hh, Wh, W2};
        disp_2d(K_DV_MAP, {&b_plane1, den_ptr[0], &b_dv1}, &pcdv, sizeof(pcdv), Wh, Hh);
        PC_Gauss1D g8h = make_gauss_pc(Hh, Wh, 0, 8.0f);
        PC_Gauss1D g8v = make_gauss_pc(Hh, Wh, 1, 8.0f);
        disp_2d(K_GAUSS_1D, {&b_dv1, &b_dv2}, &g8h, sizeof(g8h), Wh, Hh);
        disp_2d(K_GAUSS_1D, {&b_dv2, &b_dv1}, &g8v, sizeof(g8v), Wh, Hh);

        PC_GatInvQuad pcgi{H1, W1, Hh, Wh, (float)sqrt_c, (float)g_eff, (float)ped};
        disp_2d(K_GAT_INV_QUAD, {den_ptr[0], &b_dv1, &b_plane0, &b_gquad, &b_plane1}, &pcgi, sizeof(pcgi), W1, H1);

        VkBufferCopy cg{0, 0, n_1x * sizeof(float)};
        vk.fpCmdCopyBuffer(vk.cmd, b_gquad.buf, b_stage.buf, 1, &cg);
        mem_barrier();
        if (!submit_wait()) { job.out_err = "Vulkan Stage 5A DCT failed"; return false; }
    }

    for (int s = 1; s < 5; ++s) { vk.free_buf(b_pyr_s[s]); vk.free_buf(b_den_s[s]); }
    vk.free_buf(b_den0);
    vk.free_buf(b_pilot);
    vk.free_buf(b_dv1);
    vk.free_buf(b_dv2);

    double mean_g_pos = 0.05;
    cost_mark(3);
    std::vector<float> g_sub;
    {
        const float* gq = (const float*)b_stage.mapped;
        double acc = 0.0;
        for (size_t i = 0; i < n_1x; i += 16) acc += std::max(0.0f, gq[i]);
        mean_g_pos = acc / (double)((n_1x + 15) / 16);
        g_sub.reserve((size_t)(H1 / 8 + 1) * (size_t)(W1 / 8 + 1));
        for (int r = 0; r < H1; r += 8) {
            for (int c = 0; c + 2 < W1; c += 8) {
                g_sub.push_back(std::fabs(gq[(size_t)r * W1 + c + 2] - gq[(size_t)r * W1 + c]));
            }
        }
    }

    if (job.global_mean_g >= 0.0) mean_g_pos = job.global_mean_g;

    // --- Stage 5B: Chroma A-Trous + RGB Assembly + Optional RL Deconvolution ---
    VulkanEngine::GpuBuf b_tmpR, b_tmpB, b_outR;
    VulkanEngine::GpuBuf b_est, b_conv, b_gate, b_bnds;
    if (!make_buf(b_tmpR, n_1x * sizeof(float), false) ||
        !make_buf(b_tmpB, n_1x * sizeof(float), false) ||
        !make_buf(b_outR, plane2x_bytes, false)) {
        job.out_err = "Vulkan Stage 5B buffer allocation failed";
        return false;
    }

    const float resid_noise_2x = 0.35f * (float)std::sqrt(sig_eff_sq + g_eff * mean_g_pos);
    job.out_resid_noise_2x = resid_noise_2x;

    {
        begin_cb();
        if (job.chroma_denoise > 1e-4f) {
            PC_Size2D pcs1{H1, W1};
            disp_2d(K_MEDIAN3X3, {&b_dR, &b_tmpR}, &pcs1, sizeof(pcs1), W1, H1);
            if (!job.is_astro) {
                disp_2d(K_MEDIAN3X3, {&b_tmpR, &b_dR}, &pcs1, sizeof(pcs1), W1, H1);
            } else {
                std::swap(b_dR, b_tmpR);
            }
            disp_2d(K_MEDIAN3X3, {&b_dB, &b_tmpB}, &pcs1, sizeof(pcs1), W1, H1);
            if (!job.is_astro) {
                disp_2d(K_MEDIAN3X3, {&b_tmpB, &b_dB}, &pcs1, sizeof(pcs1), W1, H1);
            } else {
                std::swap(b_dB, b_tmpB);
            }

            if (job.glint_desat) {
                PC_Gauss1D g25h = make_gauss_pc(H1, W1, 0, 2.5f);
                PC_Gauss1D g25v = make_gauss_pc(H1, W1, 1, 2.5f);
                disp_2d(K_GAUSS_1D, {&b_gquad, &b_tmpR}, &g25h, sizeof(g25h), W1, H1);
                disp_2d(K_GAUSS_1D, {&b_tmpR, &b_tmpB}, &g25v, sizeof(g25v), W1, H1);
                disp_2d(K_GLINT_DESAT, {&b_gquad, &b_tmpB, &b_dR, &b_dB}, &pcs1, sizeof(pcs1), W1, H1);
            }

            float chroma_sig = gpu_stage5_chroma_sigma(job, eta_eff, mean_g_pos);
            if (chroma_sig > 1e-6f) {
                float sig_g = std::max(1e-4f, 0.5f * chroma_sig);
                float g_edge_scale = std::max(job.is_astro ? 0.14f : (3.0f * sig_g),
                    2.2f * (job.global_edge_p80 >= 0.0f ? job.global_edge_p80 : quick_pct_gpu_helper(g_sub, 0.80f)) + 0.006f);
                float inv_sg2 = 1.0f / (2.0f * g_edge_scale * g_edge_scale);
                for (int lv = 0; lv < 6; ++lv) {
                    int step = 1 << lv;
                    float lv_dec = kGpuAtrousLvDecay[(size_t)lv];
                    float sr = 2.7f * chroma_sig * lv_dec + 0.0015f;
                    float inv_sr2 = 1.0f / (2.0f * sr * sr);
                    PC_Atrous1D pah{H1, W1, Hh, Wh, 0, step, inv_sr2, inv_sr2, inv_sg2};
                    PC_Atrous1D pav{H1, W1, Hh, Wh, 1, step, inv_sr2, inv_sr2, inv_sg2};
                    disp_2d(K_ATROUS_1D, {&b_dR, &b_dB, &b_gquad, &b_boost, &b_tmpR, &b_tmpB}, &pah, sizeof(pah), W1, H1);
                    disp_2d(K_ATROUS_1D, {&b_tmpR, &b_tmpB, &b_gquad, &b_boost, &b_dR, &b_dB}, &pav, sizeof(pav), W1, H1);
                }
            }
        }

        PC_AssembleRgb pca{H1, W1, H2, W2, 1.0f / std::max(1e-4f, job.wb_r), 1.0f / std::max(1e-4f, job.wb_b)};
        disp_2d(K_ASSEMBLE_RGB, {&b_plane0, &b_dR, &b_dB, &b_outR, &b_plane1, &b_plane2}, &pca, sizeof(pca), W2, H2);
        if (!submit_wait()) { job.out_err = "Vulkan Stage 5B A-Trous failed"; return false; }
    }

    vk.free_buf(b_dR);
    vk.free_buf(b_dB);
    vk.free_buf(b_tmpR);
    vk.free_buf(b_tmpB);
    vk.free_buf(b_gquad);
    vk.free_buf(b_boost);

    // Optional PSF Richardson-Lucy Deconvolution
    cost_mark(4);
    if (job.psf_iters > 0) {
        if (make_buf(b_est, plane2x_bytes, false) &&
            make_buf(b_conv, plane2x_bytes, false) &&
            make_buf(b_gate, plane2x_bytes, false) &&
            make_buf(b_bnds, n_2x * sizeof(uint32_t), false)) {

            begin_cb();
            PC_Zero pcn{(uint32_t)n_2x};
            disp_1d(K_RL_LUM, {&b_outR, &b_plane1, &b_plane2, &b_plane0}, &pcn, sizeof(pcn), (uint32_t)n_2x);

            PC_Gauss1D g065h = make_gauss_pc(H2, W2, 0, 0.65f);
            PC_Gauss1D g065v = make_gauss_pc(H2, W2, 1, 0.65f);
            disp_2d(K_GAUSS_1D, {&b_plane0, &b_est}, &g065h, sizeof(g065h), W2, H2);
            disp_2d(K_GAUSS_1D, {&b_est, &b_conv}, &g065v, sizeof(g065v), W2, H2);

            float g_thr = std::max(0.0009f, 2.2f * resid_noise_2x);
            PC_RlPrepH pcph{H2, W2, Hh, Wh, g_thr * g_thr};
            disp_2d(K_RL_PREP_H, {&b_plane0, &b_conv, &b_gate, &b_est, &b_rlmod}, &pcph, sizeof(pcph), W2, H2);
            PC_RlPrepV pcpv{H2, W2, job.is_astro ? 1 : 0};
            disp_2d(K_RL_PREP_V, {&b_plane0, &b_est, &b_bnds, &b_conv}, &pcpv, sizeof(pcpv), W2, H2);

            const VulkanEngine::GpuBuf* p_est = &b_conv;
            const VulkanEngine::GpuBuf* p_conv = &b_est;

            PC_Gauss1D g10h = make_gauss_pc(H2, W2, 0, 1.0f);
            PC_Gauss1D g10v = make_gauss_pc(H2, W2, 1, 1.0f);
            disp_2d(K_GAUSS_1D, {&b_gate, &b_plane0}, &g10h, sizeof(g10h), W2, H2);
            disp_2d(K_GAUSS_1D, {&b_plane0, &b_gate}, &g10v, sizeof(g10v), W2, H2);

            PC_Gauss1D gpsfh = make_olpf_psf_pc(H2, W2, 0, job.psf_sigma, job.olpf_mode);
            PC_Gauss1D gpsfv = make_olpf_psf_pc(H2, W2, 1, job.psf_sigma, job.olpf_mode);
            float snr_knee = std::max(0.003f, 3.2f * resid_noise_2x);
            PC_RlRatio pcrr{(uint32_t)n_2x, snr_knee, 0.18f};

            for (int it = 0; it < job.psf_iters; ++it) {
                disp_2d(K_GAUSS_1D, {p_est, &b_plane0}, &gpsfh, sizeof(gpsfh), W2, H2);
                disp_2d(K_GAUSS_1D, {&b_plane0, p_conv}, &gpsfv, sizeof(gpsfv), W2, H2);
                disp_1d(K_RL_RATIO, {&b_outR, &b_plane1, &b_plane2, &b_gate, p_conv}, &pcrr, sizeof(pcrr), (uint32_t)n_2x);
                disp_2d(K_GAUSS_1D, {p_conv, &b_plane0}, &gpsfh, sizeof(gpsfh), W2, H2);
                disp_2d(K_GAUSS_1D, {&b_plane0, p_conv}, &gpsfv, sizeof(gpsfv), W2, H2);
                disp_1d(K_RL_UPDATE, {p_est, p_conv, &b_bnds}, &pcn, sizeof(pcn), (uint32_t)n_2x);
            }
            disp_1d(K_RL_APPLY, {&b_outR, &b_plane1, &b_plane2, p_est}, &pcn, sizeof(pcn), (uint32_t)n_2x);

            if (!submit_wait()) { job.out_err = "Vulkan RL deconv failed"; return false; }
        } else {
            job.out_err = "Vulkan RL allocation failed"; return false;
        }
        vk.free_buf(b_est);
        vk.free_buf(b_conv);
        vk.free_buf(b_gate);
        vk.free_buf(b_bnds);
    }
    vk.free_buf(b_plane0);
    vk.free_buf(b_rlmod);

    // Stream planar R, G, B back via staging buffer into interleaved output (core-only when tiled)
    cost_mark(5);
    const VulkanEngine::GpuBuf* planes[3] = {&b_outR, &b_plane1, &b_plane2};
    const int sr0_base = job.direct_stitch_rgb2x ? (job.core_y0 * 2) : 0;
    const int sc0_base = job.direct_stitch_rgb2x ? (job.core_x0 * 2) : 0;
    const int s_rows   = job.direct_stitch_rgb2x ? ((job.core_y1 - job.core_y0) * 2) : H2;
    const int s_cols   = job.direct_stitch_rgb2x ? ((job.core_x1 - job.core_x0) * 2) : W2;
    float* out_base    = job.direct_stitch_rgb2x ? job.direct_stitch_rgb2x : job.rgb_2x_out;
    const int out_str  = job.direct_stitch_rgb2x ? job.direct_w2_stride : W2;
    const int out_y0   = job.direct_stitch_rgb2x ? job.direct_y2 : 0;
    const int out_x0   = job.direct_stitch_rgb2x ? job.direct_x2 : 0;
    const int nt = std::max(1, job.n_threads);
    if (s_rows > 0 && s_cols > 0 && out_base) {
        const VkDeviceSize copy_src_off = (VkDeviceSize)(size_t(sr0_base) * W2 * sizeof(float));
        const VkDeviceSize copy_bytes   = (VkDeviceSize)(size_t(s_rows) * W2 * sizeof(float));
        for (int ch = 0; ch < 3; ++ch) {
            begin_cb();
            VkBufferCopy cc{copy_src_off, 0, copy_bytes};
            vk.fpCmdCopyBuffer(vk.cmd, planes[ch]->buf, b_stage.buf, 1, &cc);
            mem_barrier();
            if (!submit_wait()) { job.out_err = "Vulkan RGB readback failed"; return false; }

            const float* src = (const float*)b_stage.mapped;
            std::vector<heraw_sync::JoiningThread> workers;
            int rows_per = (s_rows + nt - 1) / nt;
            for (int t = 0; t < nt; ++t) {
                int r0 = t * rows_per;
                int r1 = std::min(s_rows, r0 + rows_per);
                if (r0 >= r1) break;
                workers.emplace_back([=]() {
                    for (int r = r0; r < r1; ++r) {
                        const float* srow = src + (size_t)r * W2 + sc0_base;
                        float* dst = out_base + ((size_t)(out_y0 + r) * out_str + out_x0) * 3 + ch;
                        for (int c = 0; c < s_cols; ++c) dst[c * 3] = srow[c];
                    }
                });
            }
        }
    }

    cost_mark(6);
    return true;
}

inline bool run_stage45_gpu_v3_full(Stage45GpuJob& job) {
    auto cost_start = std::chrono::steady_clock::now();
    job.out_wall_seconds.fill(0);
    job.out_device_seconds.fill(0);
    job.out_has_device_timing = false;
    auto cost_mark = [&](size_t phase) {
        const auto now = std::chrono::steady_clock::now();
        job.out_wall_seconds[phase] += std::chrono::duration<double>(now - cost_start).count();
        cost_start = now;
    };
    using namespace detail;
    auto& vk = global_vulkan();
    if (!vk.ensure_init()) {
        job.out_err = vk.init_err;
        return false;
    }
    std::lock_guard<std::mutex> lk(vk.mtx);

    const int out_scale = job.is_1x_mode ? 1 : 2;
    const int H1 = job.H1, W1 = job.W1;
    const int Hh = H1 / 2, Wh = W1 / 2;
    const int H_out = H1 * out_scale, W_out = W1 * out_scale;
    const int Eh = (H1 + 3) / 4, Ew = (W1 + 3) / 4;
    const int eta_shift = (out_scale == 2) ? 3 : 2;
    const int inv_out_s = (out_scale == 2) ? 1 : 0;
    const int full_H1 = (job.full_H1 > 0) ? job.full_H1 : H1;
    const int full_W1 = (job.full_W1 > 0) ? job.full_W1 : W1;
    const size_t n_half = size_t(Hh) * Wh;
    const size_t n_1x   = size_t(H1) * W1;
    const size_t n_out  = size_t(H_out) * W_out;
    const size_t n_eta  = size_t(Eh) * Ew;

    const size_t frame_atlas_bytes = size_t(job.N) * n_1x * sizeof(float);
    const size_t flow_atlas_bytes  = size_t(job.N) * std::max<size_t>(1, n_half) * sizeof(float) * 4;
    const bool need_fcomp = (!job.eta_only) && (job.fisher_comp_out != nullptr || job.direct_stitch_fcomp != nullptr);
    const size_t fcomp_eta_bytes   = (need_fcomp ? (n_out + n_eta) : n_eta) * sizeof(float);
    const VkDeviceSize plane_out_bytes = VkDeviceSize(n_out) * sizeof(float);
    const size_t max_ssbo_needed = std::max({frame_atlas_bytes, flow_atlas_bytes, fcomp_eta_bytes, size_t(plane_out_bytes)});
    if (vk.max_ssbo_range < max_ssbo_needed) {
        job.out_err = "Vulkan maxStorageBufferRange smaller than V3 atlas";
        return false;
    }

    auto t_s4 = std::chrono::steady_clock::now();
    job.out_device_label = "Vulkan (" + vk.dev_name + ")";

    std::vector<VulkanEngine::GpuBuf*> owned;
    struct Cleanup {
        VulkanEngine& vk;
        std::vector<VulkanEngine::GpuBuf*>& list;
        ~Cleanup() { for (auto* p : list) if (p) vk.free_buf(*p); }
    } cleanup{vk, owned};

    auto make_buf = [&](VulkanEngine::GpuBuf& b, VkDeviceSize sz, bool host_vis) -> bool {
        if (!vk.alloc_buf(b, sz, host_vis)) return false;
        owned.push_back(&b);
        return true;
    };

    std::vector<float> hdr_vec4;
    bool is_aeb = false, any_rob = false, is_bayer = false;
    float sat0 = 1.0e6f;
    v3_host::build_v3_guides_header(job, hdr_vec4, is_aeb, any_rob, sat0, is_bayer);
    (void)sat0;

    const VkDeviceSize guides_bytes = (VkDeviceSize(4 + 3 * job.N) + 2 * std::max<size_t>(1, n_half)) * sizeof(float) * 4;
    const VkDeviceSize stage_bytes = std::max<VkDeviceSize>({
        plane_out_bytes,
        (VkDeviceSize)(n_half * 6 + n_1x * 2) * sizeof(float) + VkDeviceSize(hdr_vec4.size()) * sizeof(float),
        VkDeviceSize(v3_host::kLutSize) * sizeof(float),
        VkDeviceSize(n_eta) * sizeof(float)
    });

    VulkanEngine::GpuBuf b_dummy, b_stage;
    VulkanEngine::GpuBuf b_green0, b_z, b_den, b_fcomp_eta;
    VulkanEngine::GpuBuf b_frames_all, b_flows_all, b_guides, b_icov, b_canc, b_g0ha, b_boost;
    VulkanEngine::GpuBuf b_dR, b_dB;

    if (!make_buf(b_dummy, 64, false) ||
        !make_buf(b_stage, stage_bytes, true) ||
        !make_buf(b_green0, plane_out_bytes, false) ||
        !make_buf(b_z, plane_out_bytes, false) ||
        !make_buf(b_den, plane_out_bytes, false) ||
        !make_buf(b_fcomp_eta, fcomp_eta_bytes, false) ||
        !make_buf(b_frames_all, frame_atlas_bytes, false) ||
        !make_buf(b_flows_all, flow_atlas_bytes, false) ||
        !make_buf(b_guides, guides_bytes, false) ||
        !make_buf(b_icov, std::max<size_t>(1, n_half) * sizeof(float) * 4, false) ||
        !make_buf(b_canc, n_1x * sizeof(float), false) ||
        !make_buf(b_g0ha, n_1x * sizeof(float), false) ||
        !make_buf(b_boost, std::max<size_t>(1, n_half) * sizeof(float), false) ||
        !make_buf(b_dR, n_1x * sizeof(float), false) ||
        !make_buf(b_dB, n_1x * sizeof(float), false)) {
        job.out_err = "Vulkan V3 buffer allocation failed";
        return false;
    }

    auto begin_cb = [&]() {
        vk.fpResetDescriptorPool(vk.dev, vk.ds_pool, 0);
        vk.fpResetCommandBuffer(vk.cmd, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk.fpBeginCommandBuffer(vk.cmd, &bi);
    };
    auto submit_wait = [&]() -> bool {
        if (vk.fpEndCommandBuffer(vk.cmd) != VK_SUCCESS) return false;
        vk.fpResetFences(vk.dev, 1, &vk.fence);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &vk.cmd;
        if (vk.fpQueueSubmit(vk.queue, 1, &si, vk.fence) != VK_SUCCESS) return false;
        return vk.fpWaitForFences(vk.dev, 1, &vk.fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    };
    auto mem_barrier = [&]() {
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
        vk.fpCmdPipelineBarrier(vk.cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &mb, 0, nullptr, 0, nullptr);
    };
    auto bind_and_dispatch = [&](KernelId kid, std::initializer_list<const VulkanEngine::GpuBuf*> bufs,
                                 const void* pc, uint32_t pc_sz, uint32_t gx, uint32_t gy) {
        VkDescriptorSet ds = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = vk.ds_pool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &vk.ds_layout;
        vk.fpAllocateDescriptorSets(vk.dev, &dsai, &ds);

        VkDescriptorBufferInfo dbi[6] = {};
        VkWriteDescriptorSet wds[6] = {};
        int idx = 0;
        for (const auto* b : bufs) {
            if (idx >= 6) break;
            dbi[idx] = {b->buf, 0, VK_WHOLE_SIZE};
            idx++;
        }
        for (; idx < 6; ++idx) {
            dbi[idx] = {b_dummy.buf, 0, VK_WHOLE_SIZE};
        }
        for (int i = 0; i < 6; ++i) {
            wds[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wds[i].dstSet = ds;
            wds[i].dstBinding = (uint32_t)i;
            wds[i].descriptorCount = 1;
            wds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            wds[i].pBufferInfo = &dbi[i];
        }
        vk.fpUpdateDescriptorSets(vk.dev, 6, wds, 0, nullptr);
        vk.fpCmdBindPipeline(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pipes[kid]);
        vk.fpCmdBindDescriptorSets(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pipe_layout, 0, 1, &ds, 0, nullptr);
        if (pc && pc_sz > 0) {
            vk.fpCmdPushConstants(vk.cmd, vk.pipe_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pc_sz, pc);
        }
        vk.fpCmdDispatch(vk.cmd, gx, gy, 1);
        mem_barrier();
    };
    auto disp_1d = [&](KernelId kid, std::initializer_list<const VulkanEngine::GpuBuf*> bufs,
                       const void* pc, uint32_t pc_sz, uint32_t count) {
        bind_and_dispatch(kid, bufs, pc, pc_sz, (count + 255) / 256, 1);
    };
    auto disp_2d = [&](KernelId kid, std::initializer_list<const VulkanEngine::GpuBuf*> bufs,
                       const void* pc, uint32_t pc_sz, int W, int H) {
        bind_and_dispatch(kid, bufs, pc, pc_sz, uint32_t((W + 15) / 16), uint32_t((H + 15) / 16));
    };

    // Upload static guides & header
    {
        uint8_t* st = (uint8_t*)b_stage.mapped;
        const VkDeviceSize off_icov = 0;
        const VkDeviceSize sz_icov  = std::max<size_t>(1, n_half) * sizeof(float) * 4;
        const VkDeviceSize off_canc = off_icov + sz_icov;
        const VkDeviceSize sz_canc  = n_1x * sizeof(float);
        const VkDeviceSize off_g0ha = off_canc + sz_canc;
        const VkDeviceSize sz_g0ha  = n_1x * sizeof(float);
        const VkDeviceSize off_bst  = off_g0ha + sz_g0ha;
        const VkDeviceSize sz_bst   = std::max<size_t>(1, n_half) * sizeof(float);
        const VkDeviceSize off_hdr  = off_bst + sz_bst;
        const VkDeviceSize sz_hdr   = hdr_vec4.size() * sizeof(float);

        float* dst_ic = (float*)(st + off_icov);
        for (size_t i = 0; i < n_half; ++i) {
            float axx = job.inv_cov[i * 3 + 0];
            float axy = job.inv_cov[i * 3 + 1];
            float ayy = job.inv_cov[i * 3 + 2];
            if (job.tile_spec_map) {
                eval_tile_modulated_icov(job, int(i / size_t(std::max(1, Wh))), int(i % size_t(std::max(1, Wh))), axx, axy, ayy);
            }
            dst_ic[i * 4 + 0] = axx;
            dst_ic[i * 4 + 1] = axy;
            dst_ic[i * 4 + 2] = ayy;
            dst_ic[i * 4 + 3] = job.g_guide[i];
        }
        std::memcpy(st + off_canc, job.c_anc_1x, sz_canc);
        std::memcpy(st + off_g0ha, job.g0_ha_1x, sz_g0ha);
        std::memcpy(st + off_bst, job.snr_boost_half, sz_bst);
        std::memcpy(st + off_hdr, hdr_vec4.data(), sz_hdr);

        begin_cb();
        VkBufferCopy c1{off_icov, 0, sz_icov}, c2{off_canc, 0, sz_canc}, c3{off_g0ha, 0, sz_g0ha};
        VkBufferCopy c4{off_bst, 0, sz_bst}, c5{off_hdr, 0, sz_hdr};
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_icov.buf, 1, &c1);
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_canc.buf, 1, &c2);
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_g0ha.buf, 1, &c3);
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_boost.buf, 1, &c4);
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_guides.buf, 1, &c5);
        mem_barrier();
        if (!submit_wait()) { job.out_err = "Vulkan V3 guide upload failed"; return false; }
    }

    // Upload N-frame atlas one frame at a time through compact staging buffer
    for (int n = 0; n < job.N; ++n) {
        uint8_t* st = (uint8_t*)b_stage.mapped;
        const VkDeviceSize sz_fr = VkDeviceSize(n_1x) * sizeof(float);
        const VkDeviceSize sz_fl = VkDeviceSize(std::max<size_t>(1, n_half)) * sizeof(float) * 4;
        std::memcpy(st, (*job.frames_lin)[n].data(), sz_fr);
        float* fmw = (float*)(st + sz_fr);
        const float* fy = (*job.flows_fy)[n].data();
        const float* fx = (*job.flows_fx)[n].data();
        const float* mw = (*job.mw_half)[n].data();
        for (size_t i = 0; i < n_half; ++i) {
            fmw[i * 4 + 0] = fy[i];
            fmw[i * 4 + 1] = fx[i];
            fmw[i * 4 + 2] = gpu_motion_weight(job, mw[i]);
            fmw[i * 4 + 3] = 0.0f;
        }
        begin_cb();
        VkBufferCopy cfr{0, VkDeviceSize(n) * sz_fr, sz_fr};
        VkBufferCopy cfl{sz_fr, VkDeviceSize(n) * sz_fl, sz_fl};
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_frames_all.buf, 1, &cfr);
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_flows_all.buf, 1, &cfl);
        mem_barrier();
        if (!submit_wait()) { job.out_err = "Vulkan V3 atlas upload failed"; return false; }
    }

    uint32_t cfa_w[3] = {0, 0, 0};
    pack_cfa6x6_bits(job.cfa6x6, cfa_w);
    const int fcomp_off_eta = job.eta_only ? -1 : (need_fcomp ? int(n_out) : 0);
    const size_t eta_elem_off = (fcomp_off_eta > 0) ? size_t(fcomp_off_eta) : 0;

    cost_mark(0);

    // --- CB 1: Prep Guides + Stage 4A Green Fusion ---
    {
        begin_cb();
        const VkDeviceSize eta_byte_off = VkDeviceSize(eta_elem_off) * sizeof(float);
        const VkDeviceSize eta_byte_sz  = VkDeviceSize(n_eta) * sizeof(float);
        vk.fpCmdFillBuffer(vk.cmd, b_fcomp_eta.buf, eta_byte_off, eta_byte_sz, 0);
        mem_barrier();

        PC_V3PrepGuides pcpg{H1, W1, Hh, Wh, 4 + 3 * job.N,
                             float(job.pg_sig0_sq), float(job.pg_gain),
                             cfa_w[0], cfa_w[1], cfa_w[2]};
        disp_2d(K_V3_PREP_GUIDES, {&b_icov, &b_canc, &b_boost, &b_guides}, &pcpg, sizeof(pcpg), Wh, Hh);

        PC_V3Green pcg{H1, W1, Hh, Wh, Ew, Eh, job.N, out_scale,
                       job.origin_y, job.origin_x, is_bayer ? 1 : 0, job.is_clean_plate ? 1 : 0,
                       job.is_virtual_nd ? 1 : 0, is_aeb ? 1 : 0, any_rob ? 1 : 0, fcomp_off_eta,
                       cfa_w[0], cfa_w[1], cfa_w[2]};
        disp_2d(K_V3_GREEN, {&b_frames_all, &b_flows_all, &b_guides, &b_g0ha, &b_green0, &b_fcomp_eta}, &pcg, sizeof(pcg), W1, H1);

        if (job.global_eta < 0.0 || job.eta_quarter_out != nullptr) {
            VkBufferCopy ce{eta_byte_off, 0, eta_byte_sz};
            vk.fpCmdCopyBuffer(vk.cmd, b_fcomp_eta.buf, b_stage.buf, 1, &ce);
            mem_barrier();
        }
        if (!submit_wait()) { job.out_err = "Vulkan V3 green dispatch failed"; return false; }
    }

    vk.free_buf(b_icov);
    vk.free_buf(b_canc);
    vk.free_buf(b_g0ha);

    if (job.global_eta < 0.0 || job.eta_quarter_out != nullptr)
        gpu_eta_stats(job, (const float*)b_stage.mapped, Ew, Eh);
    const double eta_eff = (job.global_eta >= 0.0) ? job.global_eta
        : std::max(0.005, job.out_eta_sum / double(std::max<int64_t>(1, job.out_eta_count)));
    job.out_eta_eff = eta_eff;
    cost_mark(1);
    if (job.eta_only) return true;

    // --- CB 2: Stage 4B Chroma-Diff Fusion ---
    {
        begin_cb();
        PC_V3Chroma pcc{H1, W1, Hh, Wh, job.N, out_scale, full_H1, full_W1,
                        job.origin_y, job.origin_x, is_bayer ? 1 : 0, job.is_xtrans ? 1 : 0,
                        job.is_virtual_nd ? 1 : 0, is_aeb ? 1 : 0, any_rob ? 1 : 0, job.is_astro ? 1 : 0,
                        cfa_w[0], cfa_w[1], cfa_w[2]};
        disp_2d(K_V3_CHROMA, {&b_frames_all, &b_flows_all, &b_guides, &b_green0, &b_dR, &b_dB}, &pcc, sizeof(pcc), W1, H1);
        if (!submit_wait()) { job.out_err = "Vulkan V3 chroma dispatch failed"; return false; }
    }
    vk.free_buf(b_frames_all);
    vk.free_buf(b_flows_all);
    vk.free_buf(b_guides);
    cost_mark(2);

    const double dt_s4 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_s4).count();
    if (job.on_stage4_done) job.on_stage4_done(eta_eff, dt_s4, job.out_device_label);

    // --- CB 3: Stage 5A NLT-VST + 64x64 COLA 2D FFT 4-Octave SURE ---
    Stage45GpuJob j5 = job;
    const bool need_green_readback = (!job.has_global_g_range || j5.global_mean_g < 0.0);
    if (need_green_readback) {
        begin_cb();
        VkBufferCopy cg{0, 0, plane_out_bytes};
        vk.fpCmdCopyBuffer(vk.cmd, b_green0.buf, b_stage.buf, 1, &cg);
        mem_barrier();
        if (!submit_wait()) { job.out_err = "Vulkan V3 green range readback failed"; return false; }
    }
    if (j5.global_mean_g < 0.0) {
        const float* pg = (const float*)b_stage.mapped;
        double m1 = 0.0;
        for (size_t i = 0; i < n_out; i += 16) m1 += std::max(0.0f, pg[i]);
        j5.global_mean_g = m1 / double((n_out + 15) / 16);
    }
    double sig2 = 0.0, gain = 0.0;
    gpu_stage5_gat_params(j5, eta_eff, sig2, gain);

    float min_g = 0.0f, max_g = 1.0f;
    if (job.has_global_g_range) {
        min_g = job.global_min_g;
        max_g = job.global_max_g;
    } else {
        const float* pg = (const float*)b_stage.mapped;
        min_g = pg[0]; max_g = pg[0];
        for (size_t i = 0; i < n_out; i += 8) {
            const float v = pg[i];
            min_g = std::min(min_g, v);
            max_g = std::max(max_g, v);
        }
    }

    const bool has_luma_noise = (sig2 + gain * std::max(0.0, j5.global_mean_g)) > 1e-16;
    if (job.denoise_strength > 1e-4f && has_luma_noise) {
        const int full_H_out = full_H1 * out_scale;
        const int origin_out_y = job.origin_y * out_scale;
        const int origin_out_x = job.origin_x * out_scale;
        const int phase_y = origin_out_y & 31;
        const int phase_x = origin_out_x & 31;
        const int nr_t = (H_out + phase_y + 63) / 32;
        const int nc_t = (W_out + phase_x + 63) / 32;
        const VkDeviceSize dv_bytes = std::max<VkDeviceSize>(64, VkDeviceSize(nr_t) * nc_t * sizeof(float));
        VulkanEngine::GpuBuf b_lut, b_dv_tile;
        if (!make_buf(b_lut, size_t(v3_host::kLutSize) * sizeof(float), false) ||
            !make_buf(b_dv_tile, dv_bytes, false)) {
            job.out_err = "Vulkan V3 Stage 5A buffer allocation failed";
            return false;
        }
        std::vector<float> lut_vec;
        float u_min = 0.0f, u_max = 1.0f, inv_du = 1.0f, oct_mul[4] = {};
        v3_host::build_v3_stage5_lut_blob(job, out_scale, eta_eff, min_g, max_g, sig2, gain,
                                          lut_vec, u_min, u_max, inv_du, oct_mul);
        std::memcpy(b_stage.mapped, lut_vec.data(), lut_vec.size() * sizeof(float));

        begin_cb();
        VkBufferCopy clut{0, 0, VkDeviceSize(lut_vec.size() * sizeof(float))};
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_lut.buf, 1, &clut);
        mem_barrier();

        PC_V3VstFwd pcvf{H_out, W_out, full_H_out, origin_out_y, u_min, u_max, inv_du, 0.0f};
        disp_2d(K_V3_VST_FWD, {&b_green0, &b_lut, &b_z}, &pcvf, sizeof(pcvf), W_out, H_out);

        vk.fpCmdFillBuffer(vk.cmd, b_den.buf, 0, plane_out_bytes, 0);
        mem_barrier();

        const float inv_mean_eta = float(1.0 / std::max(1e-4, eta_eff));
        for (int color = 0; color < 4; ++color) {
            const int ntr_c = (nr_t - (color >> 1) + 1) / 2;
            const int ntc_c = (nc_t - (color & 1) + 1) / 2;
            if (ntr_c <= 0 || ntc_c <= 0) continue;
            PC_V3FftSure pcff{H_out, W_out, Hh, Wh, Eh, Ew, int(eta_elem_off), color,
                              ntr_c, ntc_c, phase_y, phase_x, inv_out_s, eta_shift,
                              inv_mean_eta, oct_mul[0], oct_mul[1], oct_mul[2], oct_mul[3],
                              nr_t, nc_t};
            bind_and_dispatch(K_V3_FFT64_SURE, {&b_z, &b_lut, &b_boost, &b_fcomp_eta, &b_den, &b_dv_tile},
                              &pcff, sizeof(pcff), uint32_t(ntr_c) * ntc_c, 1);
        }

        PC_V3VstInv pcvi{H_out, W_out, nr_t, nc_t, full_H_out, origin_out_y, u_min, u_max, inv_du, 0.0f,
                         phase_y, phase_x};
        disp_2d(K_V3_VST_INV, {&b_den, &b_z, &b_dv_tile, &b_lut, &b_green0}, &pcvi, sizeof(pcvi), W_out, H_out);

        if (!submit_wait()) { job.out_err = "Vulkan V3 Stage 5A dispatch failed"; return false; }
        vk.free_buf(b_lut);
        vk.free_buf(b_dv_tile);
    }
    vk.free_buf(b_boost);

    const bool is_subtile = (job.direct_stitch_rgb2x != nullptr || job.origin_y != 0 || job.origin_x != 0 ||
                             H1 != full_H1 || W1 != full_W1);
    double mean_g = 0.0;
    if (is_subtile && job.global_mean_g >= 0.0) {
        mean_g = job.global_mean_g;
    } else {
        begin_cb();
        VkBufferCopy cg{0, 0, plane_out_bytes};
        vk.fpCmdCopyBuffer(vk.cmd, b_green0.buf, b_stage.buf, 1, &cg);
        mem_barrier();
        if (!submit_wait()) { job.out_err = "Vulkan V3 green mean readback failed"; return false; }
        const float* pg = (const float*)b_stage.mapped;
        double acc = 0.0;
        for (size_t i = 0; i < n_out; i += 16) acc += std::max(0.0f, pg[i]);
        mean_g = acc / double((n_out + 15) / 16);
    }
    cost_mark(3);

    // --- CB 4: Stage 5B Chroma Pyramid + Signed RGB Assembly ---
    const float sigma_c_base = gpu_stage5_chroma_sigma(job, eta_eff, mean_g);
    job.out_resid_noise_2x = 0.35f * float(std::sqrt(std::max(0.0, sig2 + gain * mean_g)));

    VulkanEngine::GpuBuf b_outR;
    if (!make_buf(b_outR, plane_out_bytes, false)) {
        job.out_err = "Vulkan V3 output R buffer allocation failed";
        return false;
    }
    VulkanEngine::GpuBuf* p_outG = &b_z;
    VulkanEngine::GpuBuf* p_outB = &b_den;

    {
        const bool run_chroma = (job.chroma_denoise > 1e-4f && sigma_c_base > 1e-7f);
        VulkanEngine::GpuBuf b_mR, b_mB, b_gpyr[4], b_rlap[4], b_blap[4], b_r_aux[4], b_b_aux[4];
        const VulkanEngine::GpuBuf* g_lvl[4] = {};
        int lh[4] = {H1, 0, 0, 0}, lw[4] = {W1, 0, 0, 0};

        if (run_chroma) {
            if (!make_buf(b_mR, n_1x * sizeof(float), false) ||
                !make_buf(b_mB, n_1x * sizeof(float), false)) {
                job.out_err = "Vulkan V3 chroma median allocation failed";
                return false;
            }
            if (out_scale == 2) {
                if (!make_buf(b_gpyr[0], n_1x * sizeof(float), false)) {
                    job.out_err = "Vulkan V3 chroma g0 allocation failed";
                    return false;
                }
                g_lvl[0] = &b_gpyr[0];
            } else {
                g_lvl[0] = &b_green0;
            }
            for (int lv = 0; lv < 4; ++lv) {
                if (lv > 0) {
                    lh[lv] = std::max(1, (lh[lv - 1] + 1) / 2);
                    lw[lv] = std::max(1, (lw[lv - 1] + 1) / 2);
                }
                const VkDeviceSize sz_lv = std::max<size_t>(1, size_t(lh[lv]) * lw[lv]) * sizeof(float);
                if (lv > 0) {
                    if (!make_buf(b_gpyr[lv], sz_lv, false)) { job.out_err = "Vulkan V3 pyr allocation failed"; return false; }
                    g_lvl[lv] = &b_gpyr[lv];
                }
                if (!make_buf(b_rlap[lv], sz_lv, false) ||
                    !make_buf(b_blap[lv], sz_lv, false) ||
                    !make_buf(b_r_aux[lv], sz_lv, false) ||
                    !make_buf(b_b_aux[lv], sz_lv, false)) {
                    job.out_err = "Vulkan V3 chroma pyramid allocation failed";
                    return false;
                }
            }
        }

        begin_cb();
        if (run_chroma) {
            if (out_scale == 2) {
                PC_V3Up2Lap pcext{H1, W1, H1, W1, 2};
                disp_2d(K_V3_UP2_LAP, {&b_green0, &b_green0, g_lvl[0]}, &pcext, sizeof(pcext), W1, H1);
            }
            PC_Size2D pcs1{H1, W1};
            disp_2d(K_MEDIAN3X3, {&b_dR, &b_mR}, &pcs1, sizeof(pcs1), W1, H1);
            disp_2d(K_MEDIAN3X3, {&b_dB, &b_mB}, &pcs1, sizeof(pcs1), W1, H1);
            PC_V3ImpClamp pcic{uint32_t(n_1x), 3.5f * sigma_c_base + 0.002f, 0.0f, 0.0f};
            disp_1d(K_V3_IMPULSE_CLAMP, {&b_dR, &b_dB, &b_mR, &b_mB}, &pcic, sizeof(pcic), uint32_t(n_1x));

            if (job.glint_desat) {
                PC_Gauss1D g2h = make_gauss_pc(H1, W1, 0, 2.0f);
                PC_Gauss1D g2v = make_gauss_pc(H1, W1, 1, 2.0f);
                disp_2d(K_GAUSS_1D, {g_lvl[0], &b_mR}, &g2h, sizeof(g2h), W1, H1);
                disp_2d(K_GAUSS_1D, {&b_mR, &b_mB}, &g2v, sizeof(g2v), W1, H1);
                disp_2d(K_GLINT_DESAT, {g_lvl[0], &b_mB, &b_dR, &b_dB}, &pcs1, sizeof(pcs1), W1, H1);
            }

            const VulkanEngine::GpuBuf* r_cur = &b_dR;
            const VulkanEngine::GpuBuf* b_cur = &b_dB;
            for (int lv = 0; lv < 3; ++lv) {
                PC_V3Down2 pcd{lh[lv], lw[lv], lh[lv + 1], lw[lv + 1]};
                disp_2d(K_V3_DOWN2, {g_lvl[lv], g_lvl[lv + 1]}, &pcd, sizeof(pcd), lw[lv + 1], lh[lv + 1]);

                const VulkanEngine::GpuBuf* r_next = (lv < 2) ? &b_r_aux[lv + 1] : &b_rlap[3];
                const VulkanEngine::GpuBuf* b_next = (lv < 2) ? &b_b_aux[lv + 1] : &b_blap[3];
                disp_2d(K_V3_DOWN2, {r_cur, r_next}, &pcd, sizeof(pcd), lw[lv + 1], lh[lv + 1]);
                disp_2d(K_V3_DOWN2, {b_cur, b_next}, &pcd, sizeof(pcd), lw[lv + 1], lh[lv + 1]);

                PC_V3Up2Lap pclap{lh[lv], lw[lv], lh[lv + 1], lw[lv + 1], 0};
                disp_2d(K_V3_UP2_LAP, {r_cur, r_next, &b_rlap[lv]}, &pclap, sizeof(pclap), lw[lv], lh[lv]);
                disp_2d(K_V3_UP2_LAP, {b_cur, b_next, &b_blap[lv]}, &pclap, sizeof(pclap), lw[lv], lh[lv]);
                r_cur = r_next;
                b_cur = b_next;
            }

            const float edge_base = (job.global_edge_p80 >= 0.0f)
                ? std::max(job.is_astro ? 0.14f : 3.0f * std::max(1e-4f, 0.5f * sigma_c_base),
                           2.2f * job.global_edge_p80 + 0.006f)
                : std::max(0.015f, 4.0f * sigma_c_base);

            const VulkanEngine::GpuBuf* r_filt[4] = {};
            const VulkanEngine::GpuBuf* b_filt[4] = {};
            for (int lv = 3; lv >= 0; --lv) {
                if (lh[lv] >= 2 && lw[lv] >= 2) {
                    const float sig_lv = sigma_c_base * std::pow(0.55f, float(lv)) * 0.85f;
                    const float edge_lv = std::max(1e-4f, edge_base * std::pow(0.85f, float(lv)));
                    const float thr_z = (lv < 3) ? (1.25f * sig_lv) : 0.0f;
                    PC_V3OddHuber pcoh{lh[lv], lw[lv], std::max(1e-6f, 2.2f * sig_lv),
                                       1.0f / (2.0f * edge_lv * edge_lv), thr_z};
                    disp_2d(K_V3_ODD_HUBER, {&b_rlap[lv], g_lvl[lv], &b_r_aux[lv]}, &pcoh, sizeof(pcoh), lw[lv], lh[lv]);
                    disp_2d(K_V3_ODD_HUBER, {&b_blap[lv], g_lvl[lv], &b_b_aux[lv]}, &pcoh, sizeof(pcoh), lw[lv], lh[lv]);
                    r_filt[lv] = &b_r_aux[lv];
                    b_filt[lv] = &b_b_aux[lv];
                } else {
                    r_filt[lv] = &b_rlap[lv];
                    b_filt[lv] = &b_blap[lv];
                }
            }

            const VulkanEngine::GpuBuf* r_rec = r_filt[3];
            const VulkanEngine::GpuBuf* b_rec = b_filt[3];
            for (int lv = 2; lv >= 0; --lv) {
                const VulkanEngine::GpuBuf* dst_r = (lv == 0) ? &b_dR : &b_rlap[lv];
                const VulkanEngine::GpuBuf* dst_b = (lv == 0) ? &b_dB : &b_blap[lv];
                PC_V3Up2Lap pcrec{lh[lv], lw[lv], lh[lv + 1], lw[lv + 1], 1};
                disp_2d(K_V3_UP2_LAP, {r_filt[lv], r_rec, dst_r}, &pcrec, sizeof(pcrec), lw[lv], lh[lv]);
                disp_2d(K_V3_UP2_LAP, {b_filt[lv], b_rec, dst_b}, &pcrec, sizeof(pcrec), lw[lv], lh[lv]);
                r_rec = dst_r;
                b_rec = dst_b;
            }
        }

        PC_V3Assemble pca{H1, W1, H_out, W_out, out_scale,
                          1.0f / std::max(1e-4f, job.wb_r), 1.0f / std::max(1e-4f, job.wb_b)};
        disp_2d(K_V3_ASSEMBLE_RGB, {&b_green0, &b_dR, &b_dB, &b_outR, p_outG, p_outB}, &pca, sizeof(pca), W_out, H_out);

        if (!submit_wait()) { job.out_err = "Vulkan V3 assembly dispatch failed"; return false; }

        if (run_chroma) {
            vk.free_buf(b_mR);
            vk.free_buf(b_mB);
            if (out_scale == 2) vk.free_buf(b_gpyr[0]);
            for (int lv = 1; lv < 4; ++lv) vk.free_buf(b_gpyr[lv]);
            for (int lv = 0; lv < 4; ++lv) {
                vk.free_buf(b_rlap[lv]);
                vk.free_buf(b_blap[lv]);
                vk.free_buf(b_r_aux[lv]);
                vk.free_buf(b_b_aux[lv]);
            }
        }
    }
    vk.free_buf(b_dR);
    vk.free_buf(b_dB);
    cost_mark(4);

    // --- CB 5: Optional Non-Deferred PSF Richardson-Lucy Deconvolution ---
    if (!job.defer_optical_psf && job.psf_iters > 0 && job.rl_mod_f16) {
        VulkanEngine::GpuBuf b_est, b_conv, b_gate, b_bnds, b_rlmod;
        const int Hq = std::max(1, H1 / 4), Wq = std::max(1, W1 / 4);
        const int mod_h = (out_scale == 2) ? Hh : Hq;
        const int mod_w = (out_scale == 2) ? Wh : Wq;
        const VkDeviceSize rl_bytes = std::max<size_t>(1, size_t(mod_h) * mod_w) * sizeof(uint32_t);
        if (!make_buf(b_est, plane_out_bytes, false) ||
            !make_buf(b_conv, plane_out_bytes, false) ||
            !make_buf(b_gate, plane_out_bytes, false) ||
            !make_buf(b_bnds, n_out * sizeof(uint32_t), false) ||
            !make_buf(b_rlmod, rl_bytes, false)) {
            job.out_err = "Vulkan V3 RL allocation failed";
            return false;
        }
        if (out_scale == 2) {
            std::memcpy(b_stage.mapped, job.rl_mod_f16, n_half * sizeof(uint32_t));
        } else {
            uint32_t* dst_rl = (uint32_t*)b_stage.mapped;
            for (int r = 0; r < Hq; ++r)
                for (int c = 0; c < Wq; ++c)
                    dst_rl[size_t(r) * Wq + c] = gpu_downsample_rlmod_q(job.rl_mod_f16, Hh, Wh, r, c);
        }

        const int it_use = (out_scale == 2) ? job.psf_iters : std::max(1, (job.psf_iters + 1) / 2 + 1);
        const float sig_use = (out_scale == 2) ? job.psf_sigma : std::max(0.42f, 0.56f * job.psf_sigma);
        const float rl_cap = (out_scale == 2) ? 0.18f : 0.15f;

        begin_cb();
        VkBufferCopy crl{0, 0, rl_bytes};
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_rlmod.buf, 1, &crl);
        mem_barrier();

        PC_Zero pcn{uint32_t(n_out)};
        disp_1d(K_RL_LUM, {&b_outR, p_outG, p_outB, &b_green0}, &pcn, sizeof(pcn), uint32_t(n_out));

        PC_Gauss1D g065h = make_gauss_pc(H_out, W_out, 0, 0.65f);
        PC_Gauss1D g065v = make_gauss_pc(H_out, W_out, 1, 0.65f);
        disp_2d(K_GAUSS_1D, {&b_green0, &b_est}, &g065h, sizeof(g065h), W_out, H_out);
        disp_2d(K_GAUSS_1D, {&b_est, &b_conv}, &g065v, sizeof(g065v), W_out, H_out);

        float g_thr = std::max(0.0009f, 2.2f * job.out_resid_noise_2x);
        PC_RlPrepH pcph{H_out, W_out, mod_h, mod_w, g_thr * g_thr};
        disp_2d(K_RL_PREP_H, {&b_green0, &b_conv, &b_gate, &b_est, &b_rlmod}, &pcph, sizeof(pcph), W_out, H_out);
        PC_RlPrepV pcpv{H_out, W_out, job.is_astro ? 1 : 0};
        disp_2d(K_RL_PREP_V, {&b_green0, &b_est, &b_bnds, &b_conv}, &pcpv, sizeof(pcpv), W_out, H_out);

        const VulkanEngine::GpuBuf* p_est = &b_conv;
        const VulkanEngine::GpuBuf* p_conv = &b_est;

        PC_Gauss1D g10h = make_gauss_pc(H_out, W_out, 0, 1.0f);
        PC_Gauss1D g10v = make_gauss_pc(H_out, W_out, 1, 1.0f);
        disp_2d(K_GAUSS_1D, {&b_gate, &b_green0}, &g10h, sizeof(g10h), W_out, H_out);
        disp_2d(K_GAUSS_1D, {&b_green0, &b_gate}, &g10v, sizeof(g10v), W_out, H_out);

        PC_Gauss1D gpsfh = make_olpf_psf_pc(H_out, W_out, 0, sig_use, job.olpf_mode);
        PC_Gauss1D gpsfv = make_olpf_psf_pc(H_out, W_out, 1, sig_use, job.olpf_mode);
        float snr_knee = std::max(0.003f, 3.2f * job.out_resid_noise_2x);
        PC_RlRatio pcrr{uint32_t(n_out), snr_knee, rl_cap};

        for (int it = 0; it < it_use; ++it) {
            disp_2d(K_GAUSS_1D, {p_est, &b_green0}, &gpsfh, sizeof(gpsfh), W_out, H_out);
            disp_2d(K_GAUSS_1D, {&b_green0, p_conv}, &gpsfv, sizeof(gpsfv), W_out, H_out);
            disp_1d(K_RL_RATIO, {&b_outR, p_outG, p_outB, &b_gate, p_conv}, &pcrr, sizeof(pcrr), uint32_t(n_out));
            disp_2d(K_GAUSS_1D, {p_conv, &b_green0}, &gpsfh, sizeof(gpsfh), W_out, H_out);
            disp_2d(K_GAUSS_1D, {&b_green0, p_conv}, &gpsfv, sizeof(gpsfv), W_out, H_out);
            disp_1d(K_RL_UPDATE, {p_est, p_conv, &b_bnds}, &pcn, sizeof(pcn), uint32_t(n_out));
        }
        disp_1d(K_RL_APPLY, {&b_outR, p_outG, p_outB, p_est}, &pcn, sizeof(pcn), uint32_t(n_out));

        if (!submit_wait()) { job.out_err = "Vulkan V3 RL deconv failed"; return false; }
        vk.free_buf(b_est);
        vk.free_buf(b_conv);
        vk.free_buf(b_gate);
        vk.free_buf(b_bnds);
        vk.free_buf(b_rlmod);
    }
    vk.free_buf(b_green0);
    cost_mark(5);

    // Readback planar R, G, B (and fcomp if requested) with core-only stitch when tiled
    const VulkanEngine::GpuBuf* planes[3] = {&b_outR, p_outG, p_outB};
    const int sr0_base = job.direct_stitch_rgb2x ? (job.core_y0 * out_scale) : 0;
    const int sc0_base = job.direct_stitch_rgb2x ? (job.core_x0 * out_scale) : 0;
    const int s_rows   = job.direct_stitch_rgb2x ? ((job.core_y1 - job.core_y0) * out_scale) : H_out;
    const int s_cols   = job.direct_stitch_rgb2x ? ((job.core_x1 - job.core_x0) * out_scale) : W_out;
    float* out_base    = job.direct_stitch_rgb2x ? job.direct_stitch_rgb2x : job.rgb_2x_out;
    float* fc_base     = job.direct_stitch_fcomp ? job.direct_stitch_fcomp : job.fisher_comp_out;
    const int out_str  = job.direct_stitch_rgb2x ? job.direct_w2_stride : W_out;
    const int out_y0   = job.direct_stitch_rgb2x ? job.direct_y2 : 0;
    const int out_x0   = job.direct_stitch_rgb2x ? job.direct_x2 : 0;
    const int nt = std::max(1, job.n_threads);
    if (s_rows > 0 && s_cols > 0 && out_base) {
        const VkDeviceSize copy_src_off = VkDeviceSize(size_t(sr0_base) * W_out * sizeof(float));
        const VkDeviceSize copy_bytes   = VkDeviceSize(size_t(s_rows) * W_out * sizeof(float));
        for (int ch = 0; ch < 3; ++ch) {
            begin_cb();
            VkBufferCopy cc{copy_src_off, 0, copy_bytes};
            vk.fpCmdCopyBuffer(vk.cmd, planes[ch]->buf, b_stage.buf, 1, &cc);
            mem_barrier();
            if (!submit_wait()) { job.out_err = "Vulkan V3 RGB readback failed"; return false; }

            const float* src = (const float*)b_stage.mapped;
            std::vector<heraw_sync::JoiningThread> workers;
            const int rows_per = (s_rows + nt - 1) / nt;
            for (int t = 0; t < nt; ++t) {
                const int r0 = t * rows_per;
                const int r1 = std::min(s_rows, r0 + rows_per);
                if (r0 >= r1) break;
                workers.emplace_back([=]() {
                    for (int r = r0; r < r1; ++r) {
                        const float* srow = src + size_t(r) * W_out + sc0_base;
                        float* dst = out_base + (size_t(out_y0 + r) * out_str + out_x0) * 3 + ch;
                        for (int c = 0; c < s_cols; ++c) dst[c * 3] = srow[c];
                    }
                });
            }
        }
        if (need_fcomp && fc_base) {
            begin_cb();
            VkBufferCopy cc{copy_src_off, 0, copy_bytes};
            vk.fpCmdCopyBuffer(vk.cmd, b_fcomp_eta.buf, b_stage.buf, 1, &cc);
            mem_barrier();
            if (!submit_wait()) { job.out_err = "Vulkan V3 fisher readback failed"; return false; }

            const float* src = (const float*)b_stage.mapped;
            for (int r = 0; r < s_rows; ++r) {
                std::copy_n(src + size_t(r) * W_out + sc0_base, s_cols,
                            fc_base + size_t(out_y0 + r) * out_str + out_x0);
            }
        }
    }

    cost_mark(6);
    return true;
}

inline bool run_stage45_gpu(Stage45GpuJob& job) {
    auto& vk = detail::global_vulkan();
    if (!vk.ensure_init()) { job.out_err = vk.init_err; return false; }
    size_t heap = 0;
    for (uint32_t i = 0; i < vk.mem_props.memoryHeapCount; ++i)
        if (vk.mem_props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            heap = std::max(heap, size_t(vk.mem_props.memoryHeaps[i].size));
    job.can_direct_stitch = true;
    if (job.math_v3_unified) {
        const size_t out_s2 = job.is_1x_mode ? 1 : 4;
        const size_t atlas_div = std::max<size_t>(16, size_t(job.N) * 4);
        const size_t per_px_budget = std::max<size_t>(16, size_t(job.N) * 8 + out_s2 * 16 + 24);
        const size_t vram_div = (heap >= (4ULL << 30)) ? 2 : 4;
        const size_t ssbo_lim_px = size_t(vk.max_ssbo_range) / atlas_div;
        if (ssbo_lim_px < 193600) {
            job.out_err = "Vulkan max_ssbo_range below minimum halo tile for N frames";
            return false;
        }
        job.max_tile_pixels = std::min({
            job.max_tile_pixels,
            ssbo_lim_px,
            heap / (vram_div * per_px_budget)
        });
        if (job.tile_size_1x == 0 && size_t(job.H1) * size_t(job.W1) > job.max_tile_pixels) {
            job.tile_size_1x = 3600;
        }
        return run_stage45_streamed(job, run_stage45_gpu_v3_full, true);
    }
    job.max_tile_pixels = std::min({job.max_tile_pixels, size_t(vk.max_ssbo_range) / 16, heap / (4 * 128)});
    return run_stage45_streamed(job, run_stage45_gpu_full, true);
}

inline bool run_sensor_residual_gpu(const SensorResidualJob& j) {
    auto& vk = detail::global_vulkan();
    if (!vk.ensure_init()) return false;
    std::lock_guard<std::mutex> lk(vk.mtx);
    const size_t pixels = size_t(j.H) * j.W;
    const VkDeviceSize bytes = (VkDeviceSize)(pixels * sizeof(float));
    const size_t half = pixels / 4;
    const VkDeviceSize scene_bytes = (VkDeviceSize)(half * 3 * sizeof(float));
    if (bytes > vk.max_ssbo_range || scene_bytes > vk.max_ssbo_range) return false;

    detail::VulkanEngine::GpuBuf b_dummy, b_stage, b_raw, b_flow, b_scene, b_res;
    struct BufGuard {
        detail::VulkanEngine& vk;
        std::vector<detail::VulkanEngine::GpuBuf*> list;
        ~BufGuard() { for (auto* b : list) vk.free_buf(*b); }
    } guard{vk, {&b_dummy, &b_stage, &b_raw, &b_flow, &b_scene, &b_res}};

    const VkDeviceSize stage_sz = std::max<VkDeviceSize>(scene_bytes, bytes * 2);
    if (!vk.alloc_buf(b_dummy, 64, false) ||
        !vk.alloc_buf(b_stage, stage_sz, true) ||
        !vk.alloc_buf(b_raw, bytes, false) ||
        !vk.alloc_buf(b_flow, bytes, false) ||
        !vk.alloc_buf(b_scene, scene_bytes, false) ||
        !vk.alloc_buf(b_res, bytes, false)) return false;

    auto begin_cb = [&]() {
        vk.fpResetDescriptorPool(vk.dev, vk.ds_pool, 0);
        vk.fpResetCommandBuffer(vk.cmd, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk.fpBeginCommandBuffer(vk.cmd, &bi);
    };
    auto submit_wait = [&]() -> bool {
        if (vk.fpEndCommandBuffer(vk.cmd) != VK_SUCCESS) return false;
        vk.fpResetFences(vk.dev, 1, &vk.fence);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &vk.cmd;
        if (vk.fpQueueSubmit(vk.queue, 1, &si, vk.fence) != VK_SUCCESS) return false;
        return vk.fpWaitForFences(vk.dev, 1, &vk.fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    };
    auto mem_barrier = [&]() {
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                           VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
        vk.fpCmdPipelineBarrier(vk.cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &mb, 0, nullptr, 0, nullptr);
    };

    std::memcpy(b_stage.mapped, j.scene, (size_t)scene_bytes);
    begin_cb();
    VkBufferCopy cs{0, 0, scene_bytes};
    vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_scene.buf, 1, &cs);
    mem_barrier();
    if (!submit_wait()) return false;

    struct Params { int32_t H, W, Hh, Wh; float read_variance, shot; uint32_t cfa[3]; };
    Params pc{j.H, j.W, j.H / 2, j.W / 2, j.read_variance, j.shot, {0, 0, 0}};
    for (int i = 0; i < 36; ++i) pc.cfa[i / 16] |= uint32_t(j.cfa[i]) << ((i % 16) * 2);

    for (size_t n = 0; n < j.raw->size(); ++n) {
        uint8_t* st = (uint8_t*)b_stage.mapped;
        std::memcpy(st, (*j.raw)[n].data(), (size_t)bytes);
        float* packed = (float*)(st + bytes);
        for (size_t i = 0; i < half; ++i) {
            packed[i * 4 + 0] = (*j.fy)[n][i];
            packed[i * 4 + 1] = (*j.fx)[n][i];
            packed[i * 4 + 2] = (*j.mask)[n][i];
            packed[i * 4 + 3] = 0.0f;
        }
        begin_cb();
        VkBufferCopy cr{0, 0, bytes}, cf{bytes, 0, bytes};
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_raw.buf, 1, &cr);
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_flow.buf, 1, &cf);
        mem_barrier();

        VkDescriptorSet ds = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = vk.ds_pool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &vk.ds_layout;
        vk.fpAllocateDescriptorSets(vk.dev, &dsai, &ds);
        const detail::VulkanEngine::GpuBuf* bufs[6] = {&b_raw, &b_flow, &b_scene, &b_res, &b_dummy, &b_dummy};
        VkDescriptorBufferInfo dbi[6] = {};
        VkWriteDescriptorSet wds[6] = {};
        for (int i = 0; i < 6; ++i) {
            dbi[i] = {bufs[i]->buf, 0, VK_WHOLE_SIZE};
            wds[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wds[i].dstSet = ds;
            wds[i].dstBinding = (uint32_t)i;
            wds[i].descriptorCount = 1;
            wds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            wds[i].pBufferInfo = &dbi[i];
        }
        vk.fpUpdateDescriptorSets(vk.dev, 6, wds, 0, nullptr);
        vk.fpCmdBindPipeline(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pipes[detail::K_SENSOR_RESIDUAL]);
        vk.fpCmdBindDescriptorSets(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pipe_layout, 0, 1, &ds, 0, nullptr);
        vk.fpCmdPushConstants(vk.cmd, vk.pipe_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vk.fpCmdDispatch(vk.cmd, (uint32_t)((j.W + 15) / 16), (uint32_t)((j.H + 15) / 16), 1);
        mem_barrier();

        VkBufferCopy co{0, 0, bytes};
        vk.fpCmdCopyBuffer(vk.cmd, b_res.buf, b_stage.buf, 1, &co);
        mem_barrier();
        if (!submit_wait()) return false;
        std::memcpy((*j.residual)[n].data(), b_stage.mapped, (size_t)bytes);
    }
    return true;
}

inline bool run_m5a_gls_gpu(M5aGlsGpuJob& job) {
    if (!job.rgb || !job.raw || job.raw->empty() || job.H1 < 2 || job.W1 < 2 ||
        (job.H1 & 1) || (job.W1 & 1) || job.N <= 0 || int(job.raw->size()) != job.N) {
        job.out_err = "Invalid M5-A GLS input dimensions";
        return false;
    }
    if (job.out_scale != 1 && job.out_scale != 2) {
        job.out_err = "Invalid M5-A out_scale";
        return false;
    }
    using namespace detail;
    auto& vk = global_vulkan();
    if (!vk.ensure_init()) {
        job.out_err = vk.init_err;
        return false;
    }
    std::lock_guard<std::mutex> lk(vk.mtx);

    const int H1 = job.H1, W1 = job.W1, N = job.N;
    const int Hh = H1 / 2, Wh = W1 / 2;
    const int scale = job.out_scale;
    const int H_out = H1 * scale, W_out = W1 * scale;
    const int tile = job.tile > 0 ? std::max(16, std::min(128, job.tile))
        : std::min(96, std::max(16, std::min(H_out, W_out) / 2));
    const int nr = (H_out + tile - 1) / tile;
    const int nc = (W_out + tile - 1) / tile;
    const int iterations = std::max(0, job.iterations);

    std::vector<std::array<float, 4>> bounds((size_t)N, {0.0f, 0.0f, 0.0f, 0.0f});
    std::vector<uint8_t> is_const_shift((size_t)N, 1);
    std::vector<uint8_t> frame_unit_mw((size_t)N, 1);
    std::vector<uint8_t> frame_all_valid((size_t)N, 0);
    std::vector<float> soft_mul((size_t)N, 1.0f);

    if (job.fy && !job.fy->empty() && job.fx && !job.fx->empty()) {
        for (int n = 0; n < N; ++n) {
            if (n >= int(job.fy->size()) || (*job.fy)[n].empty()) continue;
            const auto& fyn = (*job.fy)[n];
            const auto& fxn = (*job.fx)[n];
            float ymn = 2.0f * fyn[0], ymx = ymn, xmn = 2.0f * fxn[0], xmx = xmn;
            for (size_t i = 0; i < fyn.size(); ++i) {
                const float dy = 2.0f * fyn[i], dx = 2.0f * fxn[i];
                if (!std::isfinite(dy) || !std::isfinite(dx)) {
                    job.out_err = "Non-finite flow field in M5-A GPU";
                    return false;
                }
                ymn = std::min(ymn, dy); ymx = std::max(ymx, dy);
                xmn = std::min(xmn, dx); xmx = std::max(xmx, dx);
            }
            bounds[n] = {ymn, ymx, xmn, xmx};
            is_const_shift[n] = (ymn == ymx && xmn == xmx) ? 1 : 0;
        }
    }
    if (job.frame_sharp_scores && int(job.frame_sharp_scores->size()) >= N) {
        float s_max = 0.0f;
        for (int n = 0; n < N; ++n) s_max = std::max(s_max, (*job.frame_sharp_scores)[n]);
        if (s_max > 1e-6f) {
            for (int n = 0; n < N; ++n) {
                const float sn = std::max(1e-6f, (*job.frame_sharp_scores)[n]);
                soft_mul[n] = std::max(1.0f, std::min(1.6f, s_max / sn));
            }
        }
    }
    if (job.optical_psf_sigma_1x > 0.12f) {
        const float opt_mul = std::min(2.2f, std::hypot(1.0f, 1.25f * job.optical_psf_sigma_1x));
        for (float& sm : soft_mul) sm *= opt_mul;
    }
    for (int n = 0; n < N; ++n) {
        if (job.mw && n < int(job.mw->size())) {
            for (float m : (*job.mw)[n]) {
                if (!(m >= 1.0f)) { frame_unit_mw[n] = 0; break; }
            }
        }
        const float sat_n = job.frame_saturation_levels ? job.frame_saturation_levels[n]
                                                        : std::numeric_limits<float>::infinity();
        bool ok_all = true;
        const auto& rn = (*job.raw)[n];
        for (size_t i = 0; i < rn.size(); ++i) {
            if (!std::isfinite(rn[i]) || !(rn[i] < sat_n)) { ok_all = false; break; }
        }
        frame_all_valid[n] = ok_all ? 1 : 0;
    }

    float max_opt_shift = 0.0f;
    if (job.frame_ois_shifts) {
        for (float s : *job.frame_ois_shifts) max_opt_shift = std::max(max_opt_shift, std::fabs(s));
    }
    const float max_lca = std::max(std::fabs(job.lca_r_k1) + std::fabs(job.lca_r_k2),
                                   std::fabs(job.lca_b_k1) + std::fabs(job.lca_b_k2));
    const float margin_1x = 4.0f + max_opt_shift + 2.0f * max_lca * float(std::max(H1, W1));

    const VkDeviceSize ssbo_cap = std::min<VkDeviceSize>(vk.max_ssbo_range, VkDeviceSize(256ULL << 20));
    const int max_rows_gm = std::max(tile, int(ssbo_cap / VkDeviceSize(std::max(1, W_out * 24))));
    const int nr_band_cap = std::max(1, std::min({nr, 8, max_rows_gm / tile}));
    const int max_band_h = std::min(H_out, nr_band_cap * tile);
    const int max_rgb_in_h = std::min(H_out, max_band_h + 8);
    const VkDeviceSize gm_bytes = VkDeviceSize(max_band_h) * W_out * 6 * sizeof(int32_t);
    const VkDeviceSize rgb_in_bytes = VkDeviceSize(max_rgb_in_h) * W_out * 3 * sizeof(float);
    const VkDeviceSize rgb_out_bytes = VkDeviceSize(max_band_h) * W_out * 3 * sizeof(float);
    const VkDeviceSize tile_stats_bytes = VkDeviceSize(nr_band_cap) * nc * 136 * sizeof(uint32_t);
    const VkDeviceSize params_bytes = VkDeviceSize(48 + N * 16) * sizeof(float);
    const VkDeviceSize raw_full_bytes = VkDeviceSize(H1) * W1 * sizeof(float);
    const VkDeviceSize flow_full_bytes = VkDeviceSize(std::max(1, Hh)) * std::max(1, Wh) * 4 * sizeof(float);
    const int wg_x = (W_out + 15) / 16;
    const int max_wg_y = (max_band_h + 15) / 16;
    const VkDeviceSize wg_stats_bytes = VkDeviceSize(wg_x) * max_wg_y * 2 * sizeof(float);
    const VkDeviceSize fcomp_bytes = VkDeviceSize(max_band_h) * W_out * sizeof(float);

    if (gm_bytes > vk.max_ssbo_range || rgb_in_bytes > vk.max_ssbo_range ||
        raw_full_bytes > vk.max_ssbo_range) {
        job.out_err = "Vulkan maxStorageBufferRange too small for M5-A";
        return false;
    }

    std::vector<VulkanEngine::GpuBuf*> owned;
    struct Cleanup {
        VulkanEngine& vk;
        std::vector<VulkanEngine::GpuBuf*>& list;
        ~Cleanup() { for (auto* p : list) if (p) vk.free_buf(*p); }
    } cleanup{vk, owned};
    auto make_buf = [&](VulkanEngine::GpuBuf& b, VkDeviceSize sz, bool host_vis) -> bool {
        if (!vk.alloc_buf(b, std::max<VkDeviceSize>(sz, 64), host_vis)) return false;
        owned.push_back(&b);
        return true;
    };

    VulkanEngine::GpuBuf b_dummy, b_params, b_rgb_in, b_rgb_out, b_gm, b_tstats, b_wstats, b_fcomp, b_raw, b_flow, b_stage;
    const VkDeviceSize stage_bytes = std::max({
        rgb_in_bytes + fcomp_bytes,
        rgb_out_bytes + wg_stats_bytes + tile_stats_bytes,
        raw_full_bytes + flow_full_bytes,
        params_bytes
    });
    if (!make_buf(b_dummy, 64, false) || !make_buf(b_params, params_bytes, false) ||
        !make_buf(b_rgb_in, rgb_in_bytes, false) || !make_buf(b_rgb_out, rgb_out_bytes, false) ||
        !make_buf(b_gm, gm_bytes, false) || !make_buf(b_tstats, tile_stats_bytes, false) ||
        !make_buf(b_wstats, wg_stats_bytes, false) || !make_buf(b_fcomp, fcomp_bytes, false) ||
        !make_buf(b_raw, raw_full_bytes, false) || !make_buf(b_flow, flow_full_bytes, false) ||
        !make_buf(b_stage, stage_bytes, true)) {
        job.out_err = "Vulkan M5-A buffer allocation failed";
        return false;
    }

    auto begin_cb = [&]() {
        vk.fpResetDescriptorPool(vk.dev, vk.ds_pool, 0);
        vk.fpResetCommandBuffer(vk.cmd, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk.fpBeginCommandBuffer(vk.cmd, &bi);
    };
    auto submit_wait = [&]() -> bool {
        if (vk.fpEndCommandBuffer(vk.cmd) != VK_SUCCESS) return false;
        vk.fpResetFences(vk.dev, 1, &vk.fence);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &vk.cmd;
        if (vk.fpQueueSubmit(vk.queue, 1, &si, vk.fence) != VK_SUCCESS) return false;
        return vk.fpWaitForFences(vk.dev, 1, &vk.fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    };
    auto mem_barrier = [&]() {
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                           VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
        vk.fpCmdPipelineBarrier(vk.cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &mb, 0, nullptr, 0, nullptr);
    };
    auto bind_and_dispatch = [&](KernelId kid, std::initializer_list<const VulkanEngine::GpuBuf*> bufs,
                                 const void* pc, uint32_t pc_sz, uint32_t gx, uint32_t gy) {
        VkDescriptorSet ds = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = vk.ds_pool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &vk.ds_layout;
        vk.fpAllocateDescriptorSets(vk.dev, &dsai, &ds);
        VkDescriptorBufferInfo dbi[6] = {};
        VkWriteDescriptorSet wds[6] = {};
        int idx = 0;
        for (const auto* b : bufs) {
            if (idx >= 6) break;
            dbi[idx] = {b->buf, 0, VK_WHOLE_SIZE};
            idx++;
        }
        for (; idx < 6; ++idx) dbi[idx] = {b_dummy.buf, 0, VK_WHOLE_SIZE};
        for (int i = 0; i < 6; ++i) {
            wds[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wds[i].dstSet = ds;
            wds[i].dstBinding = (uint32_t)i;
            wds[i].descriptorCount = 1;
            wds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            wds[i].pBufferInfo = &dbi[i];
        }
        vk.fpUpdateDescriptorSets(vk.dev, 6, wds, 0, nullptr);
        vk.fpCmdBindPipeline(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pipes[kid]);
        vk.fpCmdBindDescriptorSets(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pipe_layout, 0, 1, &ds, 0, nullptr);
        if (pc && pc_sz > 0) {
            vk.fpCmdPushConstants(vk.cmd, vk.pipe_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pc_sz, pc);
        }
        vk.fpCmdDispatch(vk.cmd, gx, gy, 1);
        mem_barrier();
    };

    float min_rv = job.read_variance;
    if (job.frame_read_variances) {
        for (int n = 0; n < N; ++n) min_rv = std::min(min_rv, job.frame_read_variances[n]);
    }
    const float v_scale = std::max(1e-12f, min_rv + 0.02f * job.shot);
    uint32_t q_div = 1u;
    while (q_div * 16u < uint32_t(std::max(1, N))) q_div <<= 1;
    const float q_scale = 1.0f / float(q_div);
    const float cy_opt = 0.5f * float(H1), cx_opt = 0.5f * float(W1);
    const float inv_ropt = 1.0f / std::max(1.0f, std::max(cy_opt, cx_opt));

    {
        float* p = (float*)b_stage.mapped;
        std::memset(p, 0, (size_t)params_bytes);
        p[0] = job.lca_r_k1; p[1] = job.lca_r_k2;
        p[2] = job.lca_b_k1; p[3] = job.lca_b_k2;
        p[4] = cy_opt; p[5] = cx_opt; p[6] = inv_ropt;
        for (int r = 0; r < 6; ++r)
            for (int c = 0; c < 6; ++c) p[8 + r * 6 + c] = float(job.cfa6x6[r][c]);
        for (int n = 0; n < N; ++n) {
            float* fp = p + 48 + n * 16;
            const float rv_n = job.frame_read_variances ? job.frame_read_variances[n] : job.read_variance;
            const float sh_n = job.frame_shot_coefficients ? job.frame_shot_coefficients[n] : job.shot;
            const bool has_sat = job.frame_saturation_levels && std::isfinite(job.frame_saturation_levels[n]);
            const float sat_n = has_sat ? job.frame_saturation_levels[n] : 0.0f;
            const bool has_fl = job.fy && job.fx && n < int(job.fy->size()) && !(*job.fy)[n].empty();
            const float tobit_tau = job.wavelet_tobit_deadzone
                ? (job.wavelet_deadzone_halfwidth > 0.0f ? job.wavelet_deadzone_halfwidth : 1.5f * std::sqrt(std::max(1e-12f, rv_n)))
                : 0.0f;
            fp[0] = rv_n; fp[1] = sh_n; fp[2] = sat_n;
            fp[3] = frame_all_valid[n] ? 1.0f : 0.0f;
            fp[4] = frame_unit_mw[n] ? 1.0f : 0.0f;
            fp[5] = has_fl ? 1.0f : 0.0f;
            fp[6] = is_const_shift[n] ? 1.0f : 0.0f;
            fp[7] = bounds[n][0]; fp[8] = bounds[n][2];
            fp[9] = 1.0f / std::max(1.0f, soft_mul[n]);
            fp[10] = 0.5f * tobit_tau * tobit_tau;
            if (job.frame_ois_shifts && int(job.frame_ois_shifts->size()) >= (n + 1) * 4) {
                fp[11] = (*job.frame_ois_shifts)[n * 4 + 0];
                fp[12] = (*job.frame_ois_shifts)[n * 4 + 1];
                fp[13] = (*job.frame_ois_shifts)[n * 4 + 2];
                fp[14] = (*job.frame_ois_shifts)[n * 4 + 3];
            }
            fp[15] = has_sat ? 1.0f : 0.0f;
        }
        begin_cb();
        VkBufferCopy cp{0, 0, params_bytes};
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_params.buf, 1, &cp);
        mem_barrier();
        if (!submit_wait()) { job.out_err = "Vulkan M5-A params upload failed"; return false; }
    }

    const int n_batches = (nr + nr_band_cap - 1) / nr_band_cap;
    const size_t row_floats = size_t(W_out) * 3;
    std::vector<float> seam_prev4(4 * row_floats, 0.0f);

    auto upload_rgb_band = [&](int band_y0, int rgb_in_y0, int rgb_in_h, int seam_rows) {
        float* dst = (float*)b_stage.mapped;
        const int seam_copy = band_y0 - rgb_in_y0;
        if (seam_copy > 0) {
            const int src_row = std::max(0, seam_rows - seam_copy);
            std::memcpy(dst, seam_prev4.data() + size_t(src_row) * row_floats, size_t(seam_copy) * row_floats * sizeof(float));
        }
        const int tail_rows = rgb_in_h - seam_copy;
        if (tail_rows > 0) {
            std::memcpy(dst + size_t(seam_copy) * row_floats,
                        job.rgb + size_t(band_y0) * row_floats,
                        size_t(tail_rows) * row_floats * sizeof(float));
        }
        const VkDeviceSize sz = VkDeviceSize(rgb_in_h) * row_floats * sizeof(float);
        begin_cb();
        VkBufferCopy c{0, 0, sz};
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_rgb_in.buf, 1, &c);
        mem_barrier();
        return submit_wait();
    };

    auto slice_bounds = [&](int n, int band_y0, int band_h, int& sy0, int& sy1, int& hy0, int& hy1) {
        const float r_lo = float(band_y0 - 2) / float(scale);
        const float r_hi = float(band_y0 + band_h + 1) / float(scale);
        const float yb_lo = r_lo - margin_1x, yb_hi = r_hi + margin_1x;
        hy0 = std::max(0, std::min(Hh - 1, int(std::floor(yb_lo * 0.5f)) - 2));
        hy1 = std::max(hy0, std::min(Hh - 1, int(std::ceil(yb_hi * 0.5f)) + 2));
        sy0 = std::max(0, std::min(H1 - 1, int(std::floor(yb_lo + bounds[n][0])) - 2));
        sy1 = std::max(sy0, std::min(H1 - 1, int(std::ceil(yb_hi + bounds[n][1])) + 2));
    };

    auto run_scatter_band = [&](int band_y0, int band_h, int tr_b0, int nr_band,
                                int rgb_in_y0, int rgb_in_h, bool is_it0, bool zero_gm_only) -> bool {
        const uint32_t ts_u32 = uint32_t(nr_band * nc * 136);
        const uint32_t gm_u32 = zero_gm_only ? 0u : uint32_t(band_h * W_out * 6);
        begin_cb();
        bind_and_dispatch(K_M5A_ZERO_U32, {&b_tstats}, &ts_u32, sizeof(uint32_t), (ts_u32 + 255) / 256, 1);
        if (gm_u32 > 0) {
            bind_and_dispatch(K_M5A_ZERO_U32, {&b_gm}, &gm_u32, sizeof(uint32_t), (gm_u32 + 255) / 256, 1);
        }
        if (!submit_wait()) return false;

        for (int n = 0; n < N; ++n) {
            int sy0, sy1, hy0, hy1;
            slice_bounds(n, band_y0, band_h, sy0, sy1, hy0, hy1);
            const int sy_cnt = sy1 - sy0 + 1;
            const int hy_cnt = hy1 - hy0 + 1;
            const VkDeviceSize r_sz = VkDeviceSize(sy_cnt) * W1 * sizeof(float);
            std::memcpy(b_stage.mapped, (*job.raw)[n].data() + size_t(sy0) * W1, (size_t)r_sz);

            const bool has_fl = job.fy && job.fx && n < int(job.fy->size()) && !(*job.fy)[n].empty();
            VkDeviceSize f_sz = 0;
            if (has_fl || !frame_unit_mw[n]) {
                f_sz = VkDeviceSize(hy_cnt) * Wh * 4 * sizeof(float);
                float* fdst = (float*)((uint8_t*)b_stage.mapped + r_sz);
                const float* fy = has_fl ? (*job.fy)[n].data() : nullptr;
                const float* fx = has_fl ? (*job.fx)[n].data() : nullptr;
                const float* mw = (job.mw && n < int(job.mw->size()) && !(*job.mw)[n].empty()) ? (*job.mw)[n].data() : nullptr;
                for (int r = 0; r < hy_cnt; ++r) {
                    const size_t src_off = size_t(hy0 + r) * Wh;
                    float* drow = fdst + size_t(r) * Wh * 4;
                    for (int c = 0; c < Wh; ++c) {
                        drow[c * 4 + 0] = fy ? fy[src_off + c] : 0.0f;
                        drow[c * 4 + 1] = fx ? fx[src_off + c] : 0.0f;
                        drow[c * 4 + 2] = mw ? mw[src_off + c] : 1.0f;
                        drow[c * 4 + 3] = 0.0f;
                    }
                }
            }

            begin_cb();
            VkBufferCopy cr{0, 0, r_sz};
            vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_raw.buf, 1, &cr);
            if (f_sz > 0) {
                VkBufferCopy cf{r_sz, 0, f_sz};
                vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_flow.buf, 1, &cf);
            }
            mem_barrier();
            PC_M5aScatter pc{
                H1, W1, Hh, Wh, H_out, W_out, scale, tile,
                nr, nc, nr_band, tr_b0, band_y0, band_h, rgb_in_y0, rgb_in_h,
                sy0, sy_cnt, hy0, hy_cnt, n, is_it0 ? 1 : 0, zero_gm_only ? 1 : 0, 0,
                v_scale, job.quad, q_scale, 0.0f
            };
            bind_and_dispatch(K_M5A_SCATTER, {&b_raw, &b_flow, &b_rgb_in, &b_gm, &b_tstats, &b_params},
                              &pc, sizeof(pc), (uint32_t)((W1 + 15) / 16), (uint32_t)((sy_cnt + 15) / 16));
            if (!submit_wait()) return false;
        }
        return true;
    };

    const float dof_factor = (N >= 2) ? (float(N - 1) / float(N)) : 0.35f;
    const float tau_morozov = dof_factor * job.morozov_target;
    const float k_pi_g = (scale == 1) ? 0.12f : 0.04f;
    const float k_pi_c_coarse = (scale == 1) ? 0.08f : 0.03f;
    const float k_pi_c = (scale == 1) ? 0.45f : 0.20f;

    double total_rms_sq = 0.0;
    size_t total_chg = 0, before_cnt = 0;
    double before_chi2 = 0.0;
    int actual_iters = 0;

    if (iterations == 0) {
        int seam_rows = 0;
        for (int b = 0; b < n_batches; ++b) {
            const int tr_b0 = b * nr_band_cap, tr_b1 = std::min(nr, tr_b0 + nr_band_cap);
            const int nr_band = tr_b1 - tr_b0;
            const int band_y0 = tr_b0 * tile;
            const int band_h = std::min(H_out - band_y0, nr_band * tile);
            const int rgb_in_y0 = std::max(0, band_y0 - 4);
            const int rgb_in_h = std::min(H_out, band_y0 + band_h + 4) - rgb_in_y0;
            if (!upload_rgb_band(band_y0, rgb_in_y0, rgb_in_h, seam_rows) ||
                !run_scatter_band(band_y0, band_h, tr_b0, nr_band, rgb_in_y0, rgb_in_h, true, true)) {
                job.out_err = "Vulkan M5-A initial chi2 failed";
                return false;
            }
            const VkDeviceSize tsz = VkDeviceSize(nr_band) * nc * 136 * sizeof(uint32_t);
            begin_cb();
            VkBufferCopy ct{0, 0, tsz};
            vk.fpCmdCopyBuffer(vk.cmd, b_tstats.buf, b_stage.buf, 1, &ct);
            mem_barrier();
            if (!submit_wait()) return false;
            const uint32_t* ts = (const uint32_t*)b_stage.mapped;
            for (int t = 0; t < nr_band * nc; ++t) {
                const uint32_t* tp = ts + t * 136;
                if (tp[5] > 0) {
                    before_chi2 += (double(tp[4]) * 4294967296.0 + double(tp[3])) / 256.0;
                    before_cnt += tp[5];
                }
            }
        }
        job.out_before = before_chi2;
        job.out_samples = before_cnt;
    } else {
        for (int it = 0; it < iterations; ++it) {
            int seam_rows = 0;
            size_t iter_chg = 0;
            for (int b = 0; b < n_batches; ++b) {
                const int tr_b0 = b * nr_band_cap, tr_b1 = std::min(nr, tr_b0 + nr_band_cap);
                const int nr_band = tr_b1 - tr_b0;
                const int band_y0 = tr_b0 * tile;
                const int band_h = std::min(H_out - band_y0, nr_band * tile);
                const int rgb_in_y0 = std::max(0, band_y0 - 4);
                const int rgb_in_h = std::min(H_out, band_y0 + band_h + 4) - rgb_in_y0;
                const int wg_y = (band_h + 15) / 16;

                if (!upload_rgb_band(band_y0, rgb_in_y0, rgb_in_h, seam_rows) ||
                    !run_scatter_band(band_y0, band_h, tr_b0, nr_band, rgb_in_y0, rgb_in_h, it == 0, false)) {
                    job.out_err = "Vulkan M5-A scatter failed";
                    return false;
                }

                begin_cb();
                PC_M5aUpdate u_pc{
                    H_out, W_out, scale, tile, nc, nr_band, tr_b0, band_y0, band_h, rgb_in_y0, wg_x, q_scale,
                    tau_morozov, v_scale, job.read_variance, job.shot, job.quad, k_pi_g, k_pi_c_coarse, k_pi_c
                };
                bind_and_dispatch(K_M5A_UPDATE, {&b_rgb_in, &b_gm, &b_tstats, &b_rgb_out, &b_wstats},
                                  &u_pc, sizeof(u_pc), (uint32_t)wg_x, (uint32_t)wg_y);

                const VkDeviceSize out_sz = VkDeviceSize(band_h) * row_floats * sizeof(float);
                const VkDeviceSize wg_sz = VkDeviceSize(wg_x) * wg_y * 2 * sizeof(float);
                const VkDeviceSize ts_sz = (it == 0) ? (VkDeviceSize(nr_band) * nc * 136 * sizeof(uint32_t)) : 0;
                VkBufferCopy co{0, 0, out_sz};
                VkBufferCopy cw{0, out_sz, wg_sz};
                vk.fpCmdCopyBuffer(vk.cmd, b_rgb_out.buf, b_stage.buf, 1, &co);
                vk.fpCmdCopyBuffer(vk.cmd, b_wstats.buf, b_stage.buf, 1, &cw);
                if (ts_sz > 0) {
                    VkBufferCopy ct{0, out_sz + wg_sz, ts_sz};
                    vk.fpCmdCopyBuffer(vk.cmd, b_tstats.buf, b_stage.buf, 1, &ct);
                }
                mem_barrier();
                if (!submit_wait()) { job.out_err = "Vulkan M5-A update failed"; return false; }

                const uint8_t* mapped = (const uint8_t*)b_stage.mapped;
                const int next_seam = std::min(4, band_h);
                std::memcpy(seam_prev4.data(),
                            job.rgb + size_t(band_y0 + band_h - next_seam) * row_floats,
                            size_t(next_seam) * row_floats * sizeof(float));
                seam_rows = next_seam;
                std::memcpy(job.rgb + size_t(band_y0) * row_floats, mapped, (size_t)out_sz);

                const float* wgs = (const float*)(mapped + out_sz);
                for (int w = 0; w < wg_x * wg_y; ++w) {
                    total_rms_sq += double(wgs[w * 2 + 0]);
                    const size_t dc = size_t(wgs[w * 2 + 1] + 0.5f);
                    total_chg += dc;
                    iter_chg += dc;
                }
                if (it == 0) {
                    const uint32_t* ts = (const uint32_t*)(mapped + out_sz + wg_sz);
                    for (int t = 0; t < nr_band * nc; ++t) {
                        const uint32_t* tp = ts + t * 136;
                        if (tp[5] > 0) {
                            before_chi2 += (double(tp[4]) * 4294967296.0 + double(tp[3])) / 256.0;
                            before_cnt += tp[5];
                        }
                    }
                }
            }
            if (it == 0) {
                job.out_before = before_chi2;
                job.out_samples = before_cnt;
            }
            if (iter_chg == 0) break;
            ++actual_iters;
        }
    }

    if (job.jacobian_prox_strength > 0.0f && H_out >= 3 && W_out >= 3) {
        const int j_bh = std::min(128, max_band_h);
        const int j_nb = (H_out + j_bh - 1) / j_bh;
        std::vector<float> seam_prev1(row_floats, 0.0f);
        bool has_seam1 = false;
        for (int b = 0; b < j_nb; ++b) {
            const int band_y0 = b * j_bh;
            const int band_h = std::min(j_bh, H_out - band_y0);
            const int rgb_in_y0 = std::max(0, band_y0 - 1);
            const int rgb_in_h = std::min(H_out, band_y0 + band_h + 1) - rgb_in_y0;
            const int wg_y = (band_h + 15) / 16;

            float* st = (float*)b_stage.mapped;
            const int top_seam = band_y0 - rgb_in_y0;
            if (top_seam > 0 && has_seam1) {
                std::memcpy(st, seam_prev1.data(), row_floats * sizeof(float));
            }
            std::memcpy(st + size_t(top_seam) * row_floats,
                        job.rgb + size_t(band_y0) * row_floats,
                        size_t(rgb_in_h - top_seam) * row_floats * sizeof(float));
            const VkDeviceSize in_sz = VkDeviceSize(rgb_in_h) * row_floats * sizeof(float);
            const VkDeviceSize fc_sz = job.fisher_comp ? VkDeviceSize(band_h) * W_out * sizeof(float) : 0;
            if (fc_sz > 0) {
                std::memcpy((uint8_t*)b_stage.mapped + in_sz, job.fisher_comp + size_t(band_y0) * W_out, (size_t)fc_sz);
            }
            begin_cb();
            VkBufferCopy ci{0, 0, in_sz};
            vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_rgb_in.buf, 1, &ci);
            if (fc_sz > 0) {
                VkBufferCopy cf{in_sz, 0, fc_sz};
                vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_fcomp.buf, 1, &cf);
            }
            mem_barrier();

            PC_M5aJacobian jpc{
                H_out, W_out, band_y0, band_h, rgb_in_y0, rgb_in_h, job.fisher_comp ? 1 : 0, wg_x,
                job.jacobian_prox_strength, job.read_variance, job.shot, job.quad
            };
            bind_and_dispatch(K_M5A_JACOBIAN, {&b_rgb_in, &b_fcomp, &b_rgb_out, &b_wstats},
                              &jpc, sizeof(jpc), (uint32_t)wg_x, (uint32_t)wg_y);
            const VkDeviceSize out_sz = VkDeviceSize(band_h) * row_floats * sizeof(float);
            const VkDeviceSize wg_sz = VkDeviceSize(wg_x) * wg_y * 2 * sizeof(float);
            VkBufferCopy co{0, 0, out_sz};
            VkBufferCopy cw{0, out_sz, wg_sz};
            vk.fpCmdCopyBuffer(vk.cmd, b_rgb_out.buf, b_stage.buf, 1, &co);
            vk.fpCmdCopyBuffer(vk.cmd, b_wstats.buf, b_stage.buf, 1, &cw);
            mem_barrier();
            if (!submit_wait()) { job.out_err = "Vulkan M5-A jacobian failed"; return false; }

            const uint8_t* mapped = (const uint8_t*)b_stage.mapped;
            std::memcpy(seam_prev1.data(), job.rgb + size_t(band_y0 + band_h - 1) * row_floats, row_floats * sizeof(float));
            has_seam1 = true;
            std::memcpy(job.rgb + size_t(band_y0) * row_floats, mapped, (size_t)out_sz);
            const float* wgs = (const float*)(mapped + out_sz);
            for (int w = 0; w < wg_x * wg_y; ++w) {
                total_rms_sq += double(wgs[w * 2 + 0]);
                total_chg += size_t(wgs[w * 2 + 1] + 0.5f);
            }
        }
    }

    if (actual_iters == 0 && !(job.jacobian_prox_strength > 0.0f && total_chg > 0)) {
        job.out_after = job.out_before;
    } else {
        const int b_step = (size_t(H1) * W1 >= 262144 && n_batches >= 4) ? ((n_batches >= 8) ? 4 : 2) : 1;
        double after_chi2 = 0.0;
        size_t after_cnt = 0;
        for (int b = 0; b < n_batches; b += b_step) {
            const int tr_b0 = b * nr_band_cap, tr_b1 = std::min(nr, tr_b0 + nr_band_cap);
            const int nr_band = tr_b1 - tr_b0;
            const int band_y0 = tr_b0 * tile;
            const int band_h = std::min(H_out - band_y0, nr_band * tile);
            const int rgb_in_y0 = std::max(0, band_y0 - 4);
            const int rgb_in_h = std::min(H_out, band_y0 + band_h + 4) - rgb_in_y0;
            std::memcpy(b_stage.mapped, job.rgb + size_t(rgb_in_y0) * row_floats, size_t(rgb_in_h) * row_floats * sizeof(float));
            begin_cb();
            VkBufferCopy ci{0, 0, VkDeviceSize(rgb_in_h) * row_floats * sizeof(float)};
            vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_rgb_in.buf, 1, &ci);
            mem_barrier();
            if (!submit_wait() || !run_scatter_band(band_y0, band_h, tr_b0, nr_band, rgb_in_y0, rgb_in_h, true, true)) {
                job.out_err = "Vulkan M5-A after chi2 failed";
                return false;
            }
            const VkDeviceSize tsz = VkDeviceSize(nr_band) * nc * 136 * sizeof(uint32_t);
            begin_cb();
            VkBufferCopy ct{0, 0, tsz};
            vk.fpCmdCopyBuffer(vk.cmd, b_tstats.buf, b_stage.buf, 1, &ct);
            mem_barrier();
            if (!submit_wait()) return false;
            const uint32_t* ts = (const uint32_t*)b_stage.mapped;
            for (int t = 0; t < nr_band * nc; ++t) {
                const uint32_t* tp = ts + t * 136;
                if (tp[5] > 0) {
                    after_chi2 += (double(tp[4]) * 4294967296.0 + double(tp[3])) / 256.0;
                    after_cnt += tp[5];
                }
            }
        }
        job.out_after = (after_cnt > 0 && after_cnt != job.out_samples)
            ? after_chi2 * (double(job.out_samples) / double(after_cnt))
            : after_chi2;
    }

    job.out_changed = total_chg;
    job.out_rms_update = std::sqrt(total_rms_sq / std::max<size_t>(1, job.out_changed));
    job.out_iterations = actual_iters;
    return true;
}

inline bool run_m5b_deconv_gpu(M5bDeconvGpuJob& job) {
    if (!job.rgb || job.H <= 0 || job.W <= 0 || !std::isfinite(job.strength) ||
        !std::isfinite(job.read_variance) || !std::isfinite(job.shot) ||
        job.read_variance < 0.0f || job.shot < 0.0f) {
        job.out_err = "Invalid M5-B parameters";
        return false;
    }
    const float str = std::max(0.0f, std::min(1.0f, job.strength));
    std::vector<M5bZoneParam> zones;
    for (const auto& z : job.zones) {
        if (std::isfinite(z.cx) && std::isfinite(z.cy) &&
            std::isfinite(z.sigma_major_px) && std::isfinite(z.sigma_minor_px) &&
            std::isfinite(z.angle_rad) && z.sigma_major_px > 0.0f && z.sigma_minor_px > 0.0f) {
            zones.push_back(z);
        }
    }
    if (job.psf_iters <= 0 || str <= 0.0f || job.H < 8 || job.W < 8) return true;
    if (!job.zones.empty() && zones.empty() && job.olpf_mode == 0) return true;
    if (job.zones.empty() && job.olpf_mode == 0 && job.psf_sigma_2x <= 0.0f) return true;

    using namespace detail;
    auto& vk = global_vulkan();
    if (!vk.ensure_init()) {
        job.out_err = vk.init_err;
        return false;
    }
    std::lock_guard<std::mutex> lk(vk.mtx);

    const int H = job.H, W = job.W;
    constexpr int CORE = 40;
    const int nr = (H + CORE - 1) / CORE;
    const int nc = (W + CORE - 1) / CORE;
    const float sig_tgt = (job.out_scale == 2) ? 0.50f : 0.42f;
    const float psf_sigma_tile = (job.out_scale == 2) ? job.psf_sigma_2x : (0.5f * job.psf_sigma_2x);
    const float max_boost = 1.0f + 0.24f * std::sqrt(float(job.psf_iters)) * str;

    const VkDeviceSize ssbo_cap = std::min<VkDeviceSize>(vk.max_ssbo_range, VkDeviceSize(128ULL << 20));
    const int max_in_rows = std::max(64, int(ssbo_cap / VkDeviceSize(std::max(1, W * 12))));
    const int nr_band_cap = std::max(1, std::min({nr, 16, std::max(1, (max_in_rows - 24) / CORE)}));
    const int max_band_h = std::min(H, nr_band_cap * CORE);
    const int max_in_h = std::min(H, std::max(64, max_band_h + 24));
    const size_t row_floats = size_t(W) * 3;
    const VkDeviceSize in_bytes = VkDeviceSize(max_in_h) * row_floats * sizeof(float);
    const VkDeviceSize out_bytes = VkDeviceSize(max_band_h) * row_floats * sizeof(float);
    const VkDeviceSize params_bytes = VkDeviceSize(132 + zones.size() * 8) * sizeof(float);
    const VkDeviceSize focus_bytes = (job.focus_half && job.focus_h > 0 && job.focus_w > 0)
        ? VkDeviceSize(job.focus_h) * job.focus_w * sizeof(float) : 64;
    const VkDeviceSize tact_bytes = VkDeviceSize(nr_band_cap) * nc * sizeof(float);
    const int wg_x = (W + 15) / 16, max_wg_y = (max_band_h + 15) / 16;
    const VkDeviceSize wg_bytes = VkDeviceSize(wg_x) * max_wg_y * 2 * sizeof(float);

    if (in_bytes > vk.max_ssbo_range || out_bytes > vk.max_ssbo_range || focus_bytes > vk.max_ssbo_range) {
        job.out_err = "Vulkan maxStorageBufferRange too small for M5-B";
        return false;
    }

    std::vector<VulkanEngine::GpuBuf*> owned;
    struct Cleanup {
        VulkanEngine& vk;
        std::vector<VulkanEngine::GpuBuf*>& list;
        ~Cleanup() { for (auto* p : list) if (p) vk.free_buf(*p); }
    } cleanup{vk, owned};
    auto make_buf = [&](VulkanEngine::GpuBuf& b, VkDeviceSize sz, bool host_vis) -> bool {
        if (!vk.alloc_buf(b, std::max<VkDeviceSize>(sz, 64), host_vis)) return false;
        owned.push_back(&b);
        return true;
    };

    VulkanEngine::GpuBuf b_dummy, b_params, b_focus, b_in, b_dec, b_tact, b_wstats, b_stage;
    const VkDeviceSize stage_bytes = std::max({in_bytes, out_bytes + wg_bytes, params_bytes, focus_bytes});
    if (!make_buf(b_dummy, 64, false) || !make_buf(b_params, params_bytes, false) ||
        !make_buf(b_focus, focus_bytes, false) || !make_buf(b_in, in_bytes, false) ||
        !make_buf(b_dec, out_bytes, false) || !make_buf(b_tact, tact_bytes, false) ||
        !make_buf(b_wstats, wg_bytes, false) || !make_buf(b_stage, stage_bytes, true)) {
        job.out_err = "Vulkan M5-B buffer allocation failed";
        return false;
    }

    auto begin_cb = [&]() {
        vk.fpResetDescriptorPool(vk.dev, vk.ds_pool, 0);
        vk.fpResetCommandBuffer(vk.cmd, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk.fpBeginCommandBuffer(vk.cmd, &bi);
    };
    auto submit_wait = [&]() -> bool {
        if (vk.fpEndCommandBuffer(vk.cmd) != VK_SUCCESS) return false;
        vk.fpResetFences(vk.dev, 1, &vk.fence);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &vk.cmd;
        if (vk.fpQueueSubmit(vk.queue, 1, &si, vk.fence) != VK_SUCCESS) return false;
        return vk.fpWaitForFences(vk.dev, 1, &vk.fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    };
    auto mem_barrier = [&]() {
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                           VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
        vk.fpCmdPipelineBarrier(vk.cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &mb, 0, nullptr, 0, nullptr);
    };
    auto bind_and_dispatch = [&](KernelId kid, std::initializer_list<const VulkanEngine::GpuBuf*> bufs,
                                 const void* pc, uint32_t pc_sz, uint32_t gx, uint32_t gy) {
        VkDescriptorSet ds = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = vk.ds_pool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &vk.ds_layout;
        vk.fpAllocateDescriptorSets(vk.dev, &dsai, &ds);
        VkDescriptorBufferInfo dbi[6] = {};
        VkWriteDescriptorSet wds[6] = {};
        int idx = 0;
        for (const auto* b : bufs) {
            if (idx >= 6) break;
            dbi[idx] = {b->buf, 0, VK_WHOLE_SIZE};
            idx++;
        }
        for (; idx < 6; ++idx) dbi[idx] = {b_dummy.buf, 0, VK_WHOLE_SIZE};
        for (int i = 0; i < 6; ++i) {
            wds[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wds[i].dstSet = ds;
            wds[i].dstBinding = (uint32_t)i;
            wds[i].descriptorCount = 1;
            wds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            wds[i].pBufferInfo = &dbi[i];
        }
        vk.fpUpdateDescriptorSets(vk.dev, 6, wds, 0, nullptr);
        vk.fpCmdBindPipeline(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pipes[kid]);
        vk.fpCmdBindDescriptorSets(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pipe_layout, 0, 1, &ds, 0, nullptr);
        if (pc && pc_sz > 0) vk.fpCmdPushConstants(vk.cmd, vk.pipe_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pc_sz, pc);
        vk.fpCmdDispatch(vk.cmd, gx, gy, 1);
        mem_barrier();
    };

    {
        float* p = (float*)b_stage.mapped;
        constexpr double two_pi_64 = 6.283185307179586476925286766559 / 64.0;
        for (int k = 0; k < 64; ++k) {
            p[k] = float(std::cos(two_pi_64 * k));
            p[64 + k] = float(std::sin(two_pi_64 * k));
        }
        p[128] = p[129] = p[130] = p[131] = 0.0f;
        for (size_t z = 0; z < zones.size(); ++z) {
            float* zp = p + 132 + z * 8;
            zp[0] = zones[z].cx; zp[1] = zones[z].cy;
            zp[2] = zones[z].sigma_major_px; zp[3] = zones[z].sigma_minor_px;
            zp[4] = zones[z].angle_rad;
            zp[5] = zones[z].sigma_rgb_px[0]; zp[6] = zones[z].sigma_rgb_px[1]; zp[7] = zones[z].sigma_rgb_px[2];
        }
        begin_cb();
        VkBufferCopy cp{0, 0, params_bytes};
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_params.buf, 1, &cp);
        mem_barrier();
        if (!submit_wait()) return false;

        if (job.focus_half && job.focus_h > 0 && job.focus_w > 0) {
            std::memcpy(b_stage.mapped, job.focus_half, (size_t)focus_bytes);
            begin_cb();
            VkBufferCopy cf{0, 0, focus_bytes};
            vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_focus.buf, 1, &cf);
            mem_barrier();
            if (!submit_wait()) return false;
        }
    }

    const int has_focus = (job.focus_half && job.focus_h > 0 && job.focus_w > 0 && !job.is_astro) ? 1 : 0;
    const int n_batches = (nr + nr_band_cap - 1) / nr_band_cap;
    auto refl_y_fn = [&](int i) {
        if (H <= 1) return 0;
        while (i < 0 || i >= H) {
            if (i < 0) i = -i;
            if (i >= H) i = 2 * H - 2 - i;
        }
        return i;
    };
    std::vector<float> seam_prev52(52 * row_floats, 0.0f);
    int seam_rows = 0;
    double total_rms_sq = 0.0;
    size_t total_chg = 0;

    for (int b = 0; b < n_batches; ++b) {
        const int tr_b0 = b * nr_band_cap, tr_b1 = std::min(nr, tr_b0 + nr_band_cap);
        const int nr_band = tr_b1 - tr_b0;
        const int band_y0 = tr_b0 * CORE;
        const int band_h = std::min(H - band_y0, nr_band * CORE);
        const int last_tile_top = band_y0 + (nr_band - 1) * CORE;
        const int refl_y = (last_tile_top + 51 >= H) ? refl_y_fn(last_tile_top + 51) : band_y0;
        const int in_y0 = std::max(0, std::min(band_y0 - 12, refl_y));
        const int in_h = std::min(H, band_y0 + band_h + 12) - in_y0;
        const int wg_y = (band_h + 15) / 16;

        float* st = (float*)b_stage.mapped;
        const int seam_copy = std::min(seam_rows, band_y0 - in_y0);
        if (seam_copy > 0) {
            const int src_row = std::max(0, seam_rows - seam_copy);
            std::memcpy(st, seam_prev52.data() + size_t(src_row) * row_floats, size_t(seam_copy) * row_floats * sizeof(float));
        }
        const int tail_rows = in_h - seam_copy;
        if (tail_rows > 0) {
            std::memcpy(st + size_t(seam_copy) * row_floats,
                        job.rgb + size_t(band_y0) * row_floats,
                        size_t(tail_rows) * row_floats * sizeof(float));
        }
        const VkDeviceSize cur_in_sz = VkDeviceSize(in_h) * row_floats * sizeof(float);
        const VkDeviceSize cur_out_sz = VkDeviceSize(band_h) * row_floats * sizeof(float);
        const VkDeviceSize cur_wg_sz = VkDeviceSize(wg_x) * wg_y * 2 * sizeof(float);

        begin_cb();
        VkBufferCopy ci{0, 0, cur_in_sz};
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_in.buf, 1, &ci);
        mem_barrier();

        PC_M5bFft fpc{
            H, W, band_y0, band_h, in_y0, in_h, nr_band, nc,
            job.out_scale, job.olpf_mode, job.is_astro ? 1 : 0, int(zones.size()),
            has_focus ? job.focus_h : 0, has_focus ? job.focus_w : 0, job.psf_iters, 0,
            str, psf_sigma_tile, job.read_variance, job.shot, job.quad, max_boost, sig_tgt, 0.0f
        };
        bind_and_dispatch(K_M5B_FFT64, {&b_in, &b_params, &b_focus, &b_dec, &b_tact},
                          &fpc, sizeof(fpc), (uint32_t)(nr_band * nc), 1);

        PC_M5bClamp cpc{
            H, W, band_y0, band_h, in_y0, in_h, nr_band, nc,
            has_focus ? job.focus_h : 0, has_focus ? job.focus_w : 0, has_focus, wg_x,
            job.read_variance, job.shot, job.quad, 0.0f
        };
        bind_and_dispatch(K_M5B_CLAMP, {&b_in, &b_dec, &b_tact, &b_focus, &b_wstats},
                          &cpc, sizeof(cpc), (uint32_t)wg_x, (uint32_t)wg_y);

        VkBufferCopy co{0, 0, cur_out_sz};
        VkBufferCopy cw{0, cur_out_sz, cur_wg_sz};
        vk.fpCmdCopyBuffer(vk.cmd, b_dec.buf, b_stage.buf, 1, &co);
        vk.fpCmdCopyBuffer(vk.cmd, b_wstats.buf, b_stage.buf, 1, &cw);
        mem_barrier();
        if (!submit_wait()) { job.out_err = "Vulkan M5-B band dispatch failed"; return false; }

        const int keep_old = std::max(0, std::min(seam_rows, 52 - band_h));
        if (keep_old > 0) {
            std::memmove(seam_prev52.data(),
                         seam_prev52.data() + size_t(seam_rows - keep_old) * row_floats,
                         size_t(keep_old) * row_floats * sizeof(float));
        }
        const int copy_cur = std::min(52 - keep_old, band_h);
        std::memcpy(seam_prev52.data() + size_t(keep_old) * row_floats,
                    job.rgb + size_t(band_y0 + band_h - copy_cur) * row_floats,
                    size_t(copy_cur) * row_floats * sizeof(float));
        seam_rows = keep_old + copy_cur;

        const uint8_t* mapped = (const uint8_t*)b_stage.mapped;
        std::memcpy(job.rgb + size_t(band_y0) * row_floats, mapped, (size_t)cur_out_sz);
        const float* wgs = (const float*)(mapped + cur_out_sz);
        for (int w = 0; w < wg_x * wg_y; ++w) {
            total_rms_sq += double(wgs[w * 2 + 0]);
            total_chg += size_t(wgs[w * 2 + 1] + 0.5f);
        }
    }

    if (job.is_astro && H >= 12 && W >= 12) {
        double sky_acc = 0.0;
        size_t sky_n = 0;
        for (int y = 0; y < H; y += 8) {
            for (int x = 0; x < W; x += 8) {
                sky_acc += double(job.rgb[(size_t(y) * W + x) * 3 + 1]);
                ++sky_n;
            }
        }
        const float sky = float(sky_acc / std::max<size_t>(1, sky_n));
        const float ps = std::max(0.0f, sky);
        const float sig_sky = std::sqrt(std::max(1e-12f, job.read_variance + ps * (job.shot + job.quad * ps)));
        const float star_thr = sky + 6.0f * sig_sky;
        const float star_sig = std::max(0.45f, 0.5f * psf_sigma_tile);
        float k5[5][5], ksum = 0.0f;
        for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
            k5[dy + 2][dx + 2] = std::exp(-0.5f * float(dy * dy + dx * dx) / (star_sig * star_sig));
            ksum += k5[dy + 2][dx + 2];
        }
        for (int dy = 0; dy < 5; ++dy) for (int dx = 0; dx < 5; ++dx) k5[dy][dx] /= ksum;
        std::vector<float> orig(job.rgb, job.rgb + size_t(H) * W * 3), cur = orig, next = orig;
        std::vector<float> rel_ratio(size_t(H) * W * 3, 1.0f);
        for (int pass = 0; pass < 2; ++pass) {
            for (int y = 2; y < H - 2; ++y) {
                for (int x = 2; x < W - 2; ++x) {
                    const size_t p = (size_t(y) * W + x) * 3;
                    if (orig[p + 1] <= sky + 3.0f * sig_sky) {
                        rel_ratio[p + 0] = rel_ratio[p + 1] = rel_ratio[p + 2] = 1.0f;
                        continue;
                    }
                    for (int ch = 0; ch < 3; ++ch) {
                        float blur = 0.0f;
                        for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx)
                            blur += k5[dy + 2][dx + 2] * cur[(size_t(y + dy) * W + x + dx) * 3 + ch];
                        const float ratio = (orig[p + ch] - sky + 1e-4f) / std::max(1e-4f, blur - sky + 1e-4f);
                        rel_ratio[p + ch] = std::max(0.25f, std::min(4.0f, ratio));
                    }
                }
            }
            for (int y = 2; y < H - 2; ++y) {
                for (int x = 2; x < W - 2; ++x) {
                    const size_t p = (size_t(y) * W + x) * 3;
                    if (orig[p + 1] <= star_thr) continue;
                    for (int ch = 0; ch < 3; ++ch) {
                        float adj = 0.0f;
                        for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx)
                            adj += k5[dy + 2][dx + 2] * rel_ratio[(size_t(y + dy) * W + x + dx) * 3 + ch];
                        next[p + ch] = std::max(sky, sky + (cur[p + ch] - sky) * (1.0f + 0.35f * str * (adj - 1.0f)));
                    }
                }
            }
            std::swap(cur, next);
        }
        std::copy_n(cur.data(), size_t(H) * W * 3, job.rgb);
    }

    job.out_samples = size_t(H) * W * 3;
    job.out_changed = total_chg;
    job.out_rms_update = std::sqrt(total_rms_sq / std::max<size_t>(1, job.out_samples));
    return true;
}

inline bool run_silicon_cleanup_gpu(SiliconCleanupGpuJob& job) {
    if (!job.raw || !job.cfa || job.H < 64 || job.W < 64 || (job.H & 1) || (job.W & 1)) {
        job.out_err = "Invalid C13 GPU parameters";
        return false;
    }
    const int N = int(job.raw->size());
    if (N < 4 || N > 16) {
        job.out_err = "C13 GPU supports 4..16 frames";
        return false;
    }
    const int H1 = job.H, W1 = job.W, Hh = H1 / 2, Wh = W1 / 2;
    const size_t n_1x = size_t(H1) * W1;
    const size_t n_half = size_t(Hh) * Wh;
    const bool has_flow = job.fy && job.fx && !job.fy->empty() && !job.fx->empty();
    const bool has_mw = job.mw && !job.mw->empty();
    for (int n = 0; n < N; ++n) {
        if ((*job.raw)[n].size() != n_1x) { job.out_err = "C13 raw plane size mismatch"; return false; }
        if (has_flow && (n >= int(job.fy->size()) || (*job.fy)[n].size() != n_half || (*job.fx)[n].size() != n_half)) {
            job.out_err = "C13 flow plane size mismatch"; return false;
        }
        if (has_mw && (n >= int(job.mw->size()) || (*job.mw)[n].size() != n_half)) {
            job.out_err = "C13 mw plane size mismatch"; return false;
        }
    }

    std::vector<std::array<float, 2>> bounds_y((size_t)N, {0.0f, 0.0f});
    if (has_flow) {
        for (int n = 0; n < N; ++n) {
            float ymn = 2.0f * (*job.fy)[n][0], ymx = ymn;
            for (size_t i = 0; i < n_half; ++i) {
                const float fyv = (*job.fy)[n][i], fxv = (*job.fx)[n][i];
                if (!std::isfinite(fyv) || !std::isfinite(fxv)) {
                    job.out_err = "Non-finite flow in C13 GPU";
                    return false;
                }
                const float dy = 2.0f * fyv;
                ymn = std::min(ymn, dy);
                ymx = std::max(ymx, dy);
            }
            bounds_y[n] = {ymn, ymx};
        }
    }

    using namespace detail;
    auto& vk = global_vulkan();
    if (!vk.ensure_init()) {
        job.out_err = vk.init_err;
        return false;
    }
    std::lock_guard<std::mutex> lk(vk.mtx);

    const VkDeviceSize ssbo_cap = std::min<VkDeviceSize>(vk.max_ssbo_range, VkDeviceSize(256ULL << 20));
    const VkDeviceSize scene_bytes = VkDeviceSize(n_half) * 3 * sizeof(float);
    const VkDeviceSize res_single_bytes = VkDeviceSize(n_1x) * sizeof(float);
    const VkDeviceSize flow_full_bytes = VkDeviceSize(n_half) * 4 * sizeof(float);
    if (scene_bytes > vk.max_ssbo_range || res_single_bytes > vk.max_ssbo_range ||
        flow_full_bytes > vk.max_ssbo_range) {
        job.out_err = "Vulkan maxStorageBufferRange too small for C13";
        return false;
    }

    const int band_hh_cap = std::max(16, std::min(Hh, int(ssbo_cap / VkDeviceSize(std::max<size_t>(1, size_t(N) * Wh * 12)))));
    const VkDeviceSize warp_stack_bytes = VkDeviceSize(N) * band_hh_cap * Wh * 3 * sizeof(float);
    const VkDeviceSize fb_bytes = VkDeviceSize(band_hh_cap) * Wh * 3 * sizeof(float);

    const bool single_pass = (VkDeviceSize(N) * res_single_bytes <= ssbo_cap);
    const int res_band_h_cap = single_pass
        ? H1
        : std::max(16, std::min(H1, int(ssbo_cap / VkDeviceSize(std::max<size_t>(1, size_t(N) * W1 * 4)))));
    const VkDeviceSize res_buf_bytes = single_pass
        ? VkDeviceSize(N) * res_single_bytes
        : std::max(res_single_bytes, VkDeviceSize(N) * res_band_h_cap * W1 * sizeof(float));

    const int max_hw = std::max(H1, W1);
    const VkDeviceSize ph_bytes = VkDeviceSize(4) * max_hw * sizeof(float);
    const VkDeviceSize val_bytes = VkDeviceSize(4) * max_hw * sizeof(uint32_t);
    const int row4_frame_floats = H1 * 4;
    const int col4_frame_floats = 4 * W1;
    const int col_base_off = N * row4_frame_floats;
    const VkDeviceSize rc_atlas_bytes = VkDeviceSize(N) * (row4_frame_floats + col4_frame_floats) * sizeof(float);
    const VkDeviceSize hot_band_bytes = VkDeviceSize(res_band_h_cap) * W1 * sizeof(float);
    const int wg_x = (W1 + 15) / 16;
    const int max_wg_y = (res_band_h_cap + 15) / 16;
    const VkDeviceSize wg_bytes = VkDeviceSize(wg_x) * max_wg_y * 5 * sizeof(float);

    std::vector<VulkanEngine::GpuBuf*> owned;
    struct Cleanup {
        VulkanEngine& vk;
        std::vector<VulkanEngine::GpuBuf*>& list;
        ~Cleanup() { for (auto* p : list) if (p) vk.free_buf(*p); }
    } cleanup{vk, owned};
    auto make_buf = [&](VulkanEngine::GpuBuf& b, VkDeviceSize sz, bool host_vis) -> bool {
        if (!vk.alloc_buf(b, std::max<VkDeviceSize>(sz, 64), host_vis)) return false;
        owned.push_back(&b);
        return true;
    };

    VulkanEngine::GpuBuf b_dummy, b_scene, b_stack, b_fb, b_raw, b_flow, b_res, b_ph, b_val, b_rc, b_hot, b_wg, b_stage;
    const VkDeviceSize stage_bytes = std::max({res_single_bytes, flow_full_bytes, fb_bytes, hot_band_bytes + wg_bytes, rc_atlas_bytes});
    if (!make_buf(b_dummy, 64, false) || !make_buf(b_scene, scene_bytes, false) ||
        !make_buf(b_stack, warp_stack_bytes, false) || !make_buf(b_fb, fb_bytes, false) ||
        !make_buf(b_raw, res_single_bytes, false) || !make_buf(b_flow, flow_full_bytes, false) ||
        !make_buf(b_res, res_buf_bytes, false) || !make_buf(b_ph, ph_bytes, false) ||
        !make_buf(b_val, val_bytes, false) || !make_buf(b_rc, rc_atlas_bytes, false) ||
        !make_buf(b_hot, hot_band_bytes, false) || !make_buf(b_wg, wg_bytes, false) ||
        !make_buf(b_stage, stage_bytes, true)) {
        job.out_err = "Vulkan C13 buffer allocation failed";
        return false;
    }

    auto begin_cb = [&]() {
        vk.fpResetDescriptorPool(vk.dev, vk.ds_pool, 0);
        vk.fpResetCommandBuffer(vk.cmd, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk.fpBeginCommandBuffer(vk.cmd, &bi);
    };
    auto submit_wait = [&]() -> bool {
        if (vk.fpEndCommandBuffer(vk.cmd) != VK_SUCCESS) return false;
        vk.fpResetFences(vk.dev, 1, &vk.fence);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &vk.cmd;
        if (vk.fpQueueSubmit(vk.queue, 1, &si, vk.fence) != VK_SUCCESS) return false;
        return vk.fpWaitForFences(vk.dev, 1, &vk.fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    };
    auto mem_barrier = [&]() {
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                           VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
        vk.fpCmdPipelineBarrier(vk.cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &mb, 0, nullptr, 0, nullptr);
    };
    auto bind_and_dispatch = [&](KernelId kid, std::initializer_list<const VulkanEngine::GpuBuf*> bufs,
                                 const void* pc, uint32_t pc_sz, uint32_t gx, uint32_t gy) {
        VkDescriptorSet ds = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = vk.ds_pool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &vk.ds_layout;
        vk.fpAllocateDescriptorSets(vk.dev, &dsai, &ds);
        VkDescriptorBufferInfo dbi[6] = {};
        VkWriteDescriptorSet wds[6] = {};
        int idx = 0;
        for (const auto* b : bufs) {
            if (idx >= 6) break;
            dbi[idx] = {b->buf, 0, VK_WHOLE_SIZE};
            idx++;
        }
        for (; idx < 6; ++idx) dbi[idx] = {b_dummy.buf, 0, VK_WHOLE_SIZE};
        for (int i = 0; i < 6; ++i) {
            wds[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wds[i].dstSet = ds;
            wds[i].dstBinding = (uint32_t)i;
            wds[i].descriptorCount = 1;
            wds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            wds[i].pBufferInfo = &dbi[i];
        }
        vk.fpUpdateDescriptorSets(vk.dev, 6, wds, 0, nullptr);
        vk.fpCmdBindPipeline(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pipes[kid]);
        vk.fpCmdBindDescriptorSets(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pipe_layout, 0, 1, &ds, 0, nullptr);
        if (pc && pc_sz > 0) vk.fpCmdPushConstants(vk.cmd, vk.pipe_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pc_sz, pc);
        vk.fpCmdDispatch(vk.cmd, gx, gy, 1);
        mem_barrier();
    };

    uint32_t cfa_w[3] = {0, 0, 0};
    for (int i = 0; i < 36; ++i) cfa_w[i / 16] |= uint32_t(job.cfa[i]) << ((i % 16) * 2);

    // Step A: Half-res median scene synthesis in vertical bands
    for (int by0 = 0; by0 < Hh; by0 += band_hh_cap) {
        const int bh = std::min(band_hh_cap, Hh - by0);
        for (int n = 0; n < N; ++n) {
            int sy0 = std::max(0, std::min(H1 - 1, int(std::floor(2.0f * by0 + bounds_y[n][0])) - 3));
            int sy1 = std::max(0, std::min(H1 - 1, int(std::ceil(2.0f * (by0 + bh - 1) + bounds_y[n][1])) + 4));
            if (n == 0) {
                sy0 = std::min(sy0, std::max(0, 2 * by0 - 3));
                sy1 = std::max(sy1, std::min(H1 - 1, 2 * (by0 + bh - 1) + 4));
            }
            if (sy1 < sy0) { sy0 = 0; sy1 = 0; }
            const int sy_cnt = sy1 - sy0 + 1;
            const VkDeviceSize r_sz = VkDeviceSize(sy_cnt) * W1 * sizeof(float);
            std::memcpy(b_stage.mapped, (*job.raw)[n].data() + size_t(sy0) * W1, (size_t)r_sz);
            begin_cb();
            VkBufferCopy cr{0, 0, r_sz};
            vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_raw.buf, 1, &cr);
            mem_barrier();
            if (!submit_wait()) return false;

            if (has_flow || has_mw) {
                const VkDeviceSize f_sz = VkDeviceSize(bh) * Wh * 4 * sizeof(float);
                float* fdst = (float*)b_stage.mapped;
                const float* fy = has_flow ? (*job.fy)[n].data() + size_t(by0) * Wh : nullptr;
                const float* fx = has_flow ? (*job.fx)[n].data() + size_t(by0) * Wh : nullptr;
                const float* mw = has_mw ? (*job.mw)[n].data() + size_t(by0) * Wh : nullptr;
                for (size_t i = 0; i < size_t(bh) * Wh; ++i) {
                    fdst[i * 4 + 0] = fy ? fy[i] : 0.0f;
                    fdst[i * 4 + 1] = fx ? fx[i] : 0.0f;
                    fdst[i * 4 + 2] = mw ? mw[i] : 1.0f;
                    fdst[i * 4 + 3] = 0.0f;
                }
                begin_cb();
                VkBufferCopy cf{0, 0, f_sz};
                vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_flow.buf, 1, &cf);
                mem_barrier();
                if (!submit_wait()) return false;
            }

            begin_cb();
            PC_C13Warp wpc{
                H1, W1, Hh, Wh, sy0, sy_cnt, by0, bh,
                by0, bh, n, has_flow ? 1 : 0, has_mw ? 1 : 0, (n == 0) ? 1 : 0, 0, 0,
                cfa_w[0], cfa_w[1], cfa_w[2], 0
            };
            bind_and_dispatch(K_C13_WARP_RGB, {&b_raw, &b_flow, &b_stack, &b_fb},
                              &wpc, sizeof(wpc), (uint32_t)((Wh + 15) / 16), (uint32_t)((bh + 15) / 16));
            if (!submit_wait()) return false;
        }
        begin_cb();
        PC_C13Scene spc{Wh, by0, bh, N};
        bind_and_dispatch(K_C13_SCENE, {&b_stack, &b_fb, &b_scene},
                          &spc, sizeof(spc), (uint32_t)((Wh + 15) / 16), (uint32_t)((bh + 15) / 16));
        if (!submit_wait()) return false;
    }

    auto upload_res_band = [&](int n, int band_y0, int band_h, int raw_has_band, int& hy0, int& hy_cnt) -> bool {
        const VkDeviceSize r_sz = VkDeviceSize(raw_has_band ? band_h : H1) * W1 * sizeof(float);
        std::memcpy(b_stage.mapped, (*job.raw)[n].data() + size_t(raw_has_band ? band_y0 : 0) * W1, (size_t)r_sz);
        begin_cb();
        VkBufferCopy cr{0, 0, r_sz};
        vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_raw.buf, 1, &cr);
        mem_barrier();
        if (!submit_wait()) return false;

        hy0 = 0; hy_cnt = 1;
        if (has_flow || has_mw) {
            const float yb_lo = float(band_y0) - bounds_y[n][1] - 4.0f;
            const float yb_hi = float(band_y0 + band_h - 1) - bounds_y[n][0] + 4.0f;
            const int hlo = has_flow
                ? std::max(0, std::min(Hh - 1, int(std::floor(0.5f * std::min(float(band_y0), yb_lo))) - 2))
                : std::max(0, std::min(Hh - 1, band_y0 / 2 - 2));
            const int hhi = has_flow
                ? std::max(hlo, std::min(Hh - 1, int(std::ceil(0.5f * std::max(float(band_y0 + band_h - 1), yb_hi))) + 2))
                : std::max(hlo, std::min(Hh - 1, (band_y0 + band_h) / 2 + 2));
            hy0 = hlo;
            hy_cnt = hhi - hlo + 1;
            const VkDeviceSize f_sz = VkDeviceSize(hy_cnt) * Wh * 4 * sizeof(float);
            float* fdst = (float*)b_stage.mapped;
            const float* fy = has_flow ? (*job.fy)[n].data() + size_t(hy0) * Wh : nullptr;
            const float* fx = has_flow ? (*job.fx)[n].data() + size_t(hy0) * Wh : nullptr;
            const float* mw = has_mw ? (*job.mw)[n].data() + size_t(hy0) * Wh : nullptr;
            for (size_t i = 0; i < size_t(hy_cnt) * Wh; ++i) {
                fdst[i * 4 + 0] = fy ? fy[i] : 0.0f;
                fdst[i * 4 + 1] = fx ? fx[i] : 0.0f;
                fdst[i * 4 + 2] = mw ? mw[i] : 1.0f;
                fdst[i * 4 + 3] = 0.0f;
            }
            begin_cb();
            VkBufferCopy cf{0, 0, f_sz};
            vk.fpCmdCopyBuffer(vk.cmd, b_stage.buf, b_flow.buf, 1, &cf);
            mem_barrier();
            if (!submit_wait()) return false;
        }
        return true;
    };

    // Step B: Sensor residual + Row/Col 4-phase medians + 97-tap highpass
    for (int n = 0; n < N; ++n) {
        int hy0 = 0, hy_cnt = 1;
        if (!upload_res_band(n, 0, H1, 0, hy0, hy_cnt)) return false;
        const int res_off = single_pass ? int(size_t(n) * n_1x) : 0;
        const int row4_off = n * row4_frame_floats;
        const int col4_off = col_base_off + n * col4_frame_floats;

        begin_cb();
        PC_C13SensorRes srpc{
            H1, W1, Hh, Wh, 0, H1, hy0, hy_cnt, res_off, 0, has_flow ? 1 : 0, has_mw ? 1 : 0,
            job.read_variance, job.shot, cfa_w[0], cfa_w[1], cfa_w[2], 0
        };
        bind_and_dispatch(K_C13_SENSOR_RES, {&b_raw, &b_flow, &b_scene, &b_res},
                          &srpc, sizeof(srpc), (uint32_t)((W1 + 15) / 16), (uint32_t)((H1 + 15) / 16));

        PC_C13Med4Ph rmpc{H1, W1, 0, H1, res_off, 0, 0, 0};
        bind_and_dispatch(K_C13_MED_4PH, {&b_res, &b_rc, &b_ph, &b_val},
                          &rmpc, sizeof(rmpc), (uint32_t)H1, 1);

        PC_C13Hp97 rhpc{H1, 0, row4_off, 0};
        bind_and_dispatch(K_C13_HP97, {&b_ph, &b_val, &b_rc},
                          &rhpc, sizeof(rhpc), (uint32_t)((H1 * 4 + 255) / 256), 1);

        PC_C13Med4Ph cmpc{H1, W1, 1, W1, res_off, row4_off, 0, 0};
        bind_and_dispatch(K_C13_MED_4PH, {&b_res, &b_rc, &b_ph, &b_val},
                          &cmpc, sizeof(cmpc), (uint32_t)W1, 1);

        PC_C13Hp97 chpc{W1, 1, col4_off, 0};
        bind_and_dispatch(K_C13_HP97, {&b_ph, &b_val, &b_rc},
                          &chpc, sizeof(chpc), (uint32_t)((W1 * 4 + 255) / 256), 1);

        if (!submit_wait()) { job.out_err = "Vulkan C13 row/col FPN failed"; return false; }
    }

    std::vector<float> rc_host(size_t(N) * (row4_frame_floats + col4_frame_floats));
    begin_cb();
    VkBufferCopy crc{0, 0, rc_atlas_bytes};
    vk.fpCmdCopyBuffer(vk.cmd, b_rc.buf, b_stage.buf, 1, &crc);
    mem_barrier();
    if (!submit_wait()) return false;
    std::memcpy(rc_host.data(), b_stage.mapped, (size_t)rc_atlas_bytes);

    // Step C: Consensus hot pixels + statistics
    const float p_h = 0.075f;
    const float hot_sigma = std::sqrt(std::max(1e-12f, job.read_variance + p_h * (job.shot + job.quad * p_h)));
    const float hot_thr = 4.0f * hot_sigma;
    std::vector<float> hot_host(n_1x, 0.0f);
    double tot_before = 0.0, tot_after = 0.0, tot_rms = 0.0;
    size_t tot_samples = 0, tot_changed = 0;

    for (int band_y0 = 0; band_y0 < H1; band_y0 += res_band_h_cap) {
        const int band_h = std::min(res_band_h_cap, H1 - band_y0);
        if (!single_pass) {
            for (int n = 0; n < N; ++n) {
                int hy0 = 0, hy_cnt = 1;
                if (!upload_res_band(n, band_y0, band_h, 1, hy0, hy_cnt)) return false;
                begin_cb();
                PC_C13SensorRes srpc{
                    H1, W1, Hh, Wh, band_y0, band_h, hy0, hy_cnt, n * band_h * W1, 1,
                    has_flow ? 1 : 0, has_mw ? 1 : 0, job.read_variance, job.shot, cfa_w[0], cfa_w[1], cfa_w[2], 0
                };
                bind_and_dispatch(K_C13_SENSOR_RES, {&b_raw, &b_flow, &b_scene, &b_res},
                                  &srpc, sizeof(srpc), (uint32_t)((W1 + 15) / 16), (uint32_t)((band_h + 15) / 16));
                if (!submit_wait()) return false;
            }
        }
        const int wg_y = (band_h + 15) / 16;
        const VkDeviceSize cur_hot_sz = VkDeviceSize(band_h) * W1 * sizeof(float);
        const VkDeviceSize cur_wg_sz = VkDeviceSize(wg_x) * wg_y * 5 * sizeof(float);
        begin_cb();
        PC_C13HotMap hpc{H1, W1, N, band_y0, band_h, wg_x, col_base_off, 0, hot_sigma, hot_thr, 0.0f, 0.0f};
        bind_and_dispatch(K_C13_HOT_MAP, {&b_res, &b_rc, &b_hot, &b_wg},
                          &hpc, sizeof(hpc), (uint32_t)wg_x, (uint32_t)wg_y);
        VkBufferCopy ch{0, 0, cur_hot_sz};
        VkBufferCopy cw{0, cur_hot_sz, cur_wg_sz};
        vk.fpCmdCopyBuffer(vk.cmd, b_hot.buf, b_stage.buf, 1, &ch);
        vk.fpCmdCopyBuffer(vk.cmd, b_wg.buf, b_stage.buf, 1, &cw);
        mem_barrier();
        if (!submit_wait()) return false;

        const uint8_t* mapped = (const uint8_t*)b_stage.mapped;
        std::memcpy(hot_host.data() + size_t(band_y0) * W1, mapped, (size_t)cur_hot_sz);
        const float* wgs = (const float*)(mapped + cur_hot_sz);
        for (int w = 0; w < wg_x * wg_y; ++w) {
            tot_before += double(wgs[w * 5 + 0]);
            tot_after += double(wgs[w * 5 + 1]);
            tot_rms += double(wgs[w * 5 + 2]);
            tot_samples += size_t(wgs[w * 5 + 3] + 0.5f);
            tot_changed += size_t(wgs[w * 5 + 4] + 0.5f);
        }
    }

    // Step D: Multi-threaded host subtraction raw[n][pix] -= row4 + col4 + hot
    const int p_nt = (job.n_threads > 1 && H1 >= 64) ? std::min(job.n_threads, 8) : 1;
    auto apply_rows = [&](int r0, int r1) {
        const float* row4_base = rc_host.data();
        const float* col4_base = rc_host.data() + col_base_off;
        for (int n = 0; n < N; ++n) {
            float* raw_n = (*job.raw)[n].data();
            const float* row4_n = row4_base + size_t(n) * row4_frame_floats;
            const float* col4_n = col4_base + size_t(n) * col4_frame_floats;
            for (int r = r0; r < r1; ++r) {
                const float* r4 = row4_n + size_t(r) * 4;
                const float* c4 = col4_n + size_t(r & 3) * W1;
                const float* ht = hot_host.data() + size_t(r) * W1;
                float* dst = raw_n + size_t(r) * W1;
                int c = 0;
                for (; c + 4 <= W1; c += 4) {
                    dst[c + 0] -= (r4[0] + c4[c + 0] + ht[c + 0]);
                    dst[c + 1] -= (r4[1] + c4[c + 1] + ht[c + 1]);
                    dst[c + 2] -= (r4[2] + c4[c + 2] + ht[c + 2]);
                    dst[c + 3] -= (r4[3] + c4[c + 3] + ht[c + 3]);
                }
                for (; c < W1; ++c) dst[c] -= (r4[c & 3] + c4[c] + ht[c]);
            }
        }
    };
    if (p_nt <= 1) {
        apply_rows(0, H1);
    } else {
        std::vector<heraw_sync::JoiningThread> workers;
        workers.reserve(p_nt);
        const int ch = (H1 + p_nt - 1) / p_nt;
        for (int t = 0; t < p_nt; ++t) {
            const int r0 = t * ch, r1 = std::min(H1, r0 + ch);
            if (r0 < r1) workers.emplace_back([&, r0, r1]() { apply_rows(r0, r1); });
        }
    }

    job.out_before = tot_before;
    job.out_after = tot_after;
    job.out_samples = tot_samples;
    job.out_changed = tot_changed;
    job.out_rms_update = std::sqrt(tot_rms / std::max<size_t>(1, n_1x * N));
    job.out_peak_scratch_bytes = (size_t(n_half) * 3 + size_t(N) * (n_1x + 4 * H1 + 4 * W1) + n_1x) * sizeof(float);
    return true;
}

} // namespace heraw_gpu

#endif // !__APPLE__
