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
    {heraw_spirv::k_sensor_residual_spv, sizeof(heraw_spirv::k_sensor_residual_spv)}
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
            dst_ic[i * 4 + 0] = job.inv_cov[i * 3 + 0];
            dst_ic[i * 4 + 1] = job.inv_cov[i * 3 + 1];
            dst_ic[i * 4 + 2] = job.inv_cov[i * 3 + 2];
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
            std::vector<std::thread> workers;
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
            for (auto& th : workers) th.join();
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
                         (float)job.pg_sig0_sq, (float)job.pg_gain, fw_s, job.origin_y, job.origin_x};
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
                          odry, odrx, odby, odbx, job.full_H1, job.full_W1, job.origin_y, job.origin_x};
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
    const double g_eff = std::max(0.0, job.pg_gain * eta_eff);
    const double sig_eff_sq = std::max(1e-10, job.pg_sig0_sq * eta_eff);
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
                float sig_s = base_sig[s] * job.denoise_strength;
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

            float wb_max = std::max(job.wb_r, job.wb_b);
            float chroma_sig = job.chroma_denoise * (float)std::sqrt((1.0 + wb_max * wb_max) * eta_eff * (job.pg_sig0_sq + job.pg_gain * mean_g_pos));
            if (chroma_sig > 1e-6f) {
                float sig_g = std::max(1e-4f, 0.5f * chroma_sig);
                float g_edge_scale = std::max(job.is_astro ? 0.14f : (3.0f * sig_g),
                    2.2f * (job.global_edge_p80 >= 0.0f ? job.global_edge_p80 : quick_pct_gpu_helper(g_sub, 0.80f)) + 0.006f);
                float inv_sg2 = 1.0f / (2.0f * g_edge_scale * g_edge_scale);
                for (int lv = 0; lv < 6; ++lv) {
                    int step = 1 << lv;
                    float lv_dec = std::pow(0.56f, (float)lv);
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
            std::vector<std::thread> workers;
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
            for (auto& th : workers) th.join();
        }
    }

    cost_mark(6);
    return true;
}

inline bool run_stage45_gpu(Stage45GpuJob& job) {
    auto& vk=detail::global_vulkan();
    if(!vk.ensure_init()) {job.out_err=vk.init_err;return false;}
    size_t heap=0;
    for(uint32_t i=0;i<vk.mem_props.memoryHeapCount;++i)
        if(vk.mem_props.memoryHeaps[i].flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            heap=std::max(heap,size_t(vk.mem_props.memoryHeaps[i].size));
    job.max_tile_pixels=std::min({job.max_tile_pixels,size_t(vk.max_ssbo_range)/16,heap/(4*128)});
    job.can_direct_stitch = true;
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

} // namespace heraw_gpu

#endif // !__APPLE__
