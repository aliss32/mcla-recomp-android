#!/system/bin/sh
# Run once per boot with the device's "Run script as root" option.
# Enables the Adreno performance counters for user processes (os/android/tools/kgslperf).
# Nothing is installed; a reboot restores the default.
echo 1 > /sys/class/kgsl/kgsl-3d0/perfcounter
cat /sys/class/kgsl/kgsl-3d0/perfcounter > /sdcard/gpu_counters_state.txt
