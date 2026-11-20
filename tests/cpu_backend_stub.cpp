#include "../heraw_gpu_backend.hpp"

namespace heraw_gpu {
#ifdef __APPLE__
bool run_sensor_residual_gpu(const SensorResidualJob&) { return false; }
#endif
GpuDeviceInfo probe_gpu_device() { return {}; }
bool run_stage45_gpu(Stage45GpuJob& job) {
    job.out_err = "CPU test build";
    return false;
}
bool run_m5a_gls_gpu(M5aGlsGpuJob& job) {
    job.out_err = "CPU test build";
    return false;
}
bool run_m5b_deconv_gpu(M5bDeconvGpuJob& job) {
    job.out_err = "CPU test build";
    return false;
}
bool run_silicon_cleanup_gpu(SiliconCleanupGpuJob& job) {
    job.out_err = "CPU test build";
    return false;
}
}
