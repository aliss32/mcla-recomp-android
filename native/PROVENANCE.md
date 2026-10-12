# The Vulkan driver proxy

Taken from andrewnakas/skate3-android (MIT), which carries it byte-identical
from AlanConstantino/skate3-pocket at `c9868d4`. Changes here: the JNI entry
points are renamed for `com.libertyrecomp.DriverBridge`, the log tag and probe
application names say Liberty, and the CMake project is renamed.

`vendor/libadrenotools` is <https://github.com/bylaws/libadrenotools> (BSD-2)
at `8fae8ce254dfc1344527e05301e43f37dea2df80`, with `lib/linkernsbypass`
(<https://github.com/bylaws/liblinkernsbypass>) at
`aa3975893d83ef1bc84c321ec60c65fbf1287887`.

## How it takes over

librexruntime.so opens Vulkan by bare name (`rex::platform::lib_names::
kVulkanLoader`, used from `src/ui/vulkan/vulkan_instance.cpp`). The APK ships
this proxy as `libvulkan.so` in nativeLibraryDir; `DriverBridge` loads it with
`System.load` before SDL loads libmain.so, so the runtime's later
`dlopen("libvulkan.so")` returns the proxy. `nativeInit` verifies that with
`RTLD_NOLOAD`.

With Turnip selected, libadrenotools loads the bundled Mesa Turnip build
(`app/src/main/assets/drivers/turnip-r6.zip`, StevenMXZ Turnip 26.3.0-R6, a6xx/a7xx) from
app-private storage, and every instance is checked to really be the custom
driver. With System selected, calls go to `/system/lib64/libvulkan.so`.

Native libraries must be extracted (`useLegacyPackaging = true`): the
adrenotools hook libraries `libmain_hook.so` and `libhook_impl.so` have to be
real files in nativeLibraryDir.
