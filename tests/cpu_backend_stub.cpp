#include "../heraw_gpu_backend.hpp"

namespace heraw_gpu {
GpuDeviceInfo probe_gpu_device() { return {}; }
bool run_stage45_gpu(Stage45GpuJob& job) {
    job.out_err = "CPU test build";
    return false;
}
}
