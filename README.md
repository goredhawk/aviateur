# Aviateur

Cross-platform OpenIPC FPV ground station. Forked from [fpv4win](https://github.com/OpenIPC/fpv4win]).

![](tutorials/interface.jpg)

Supports RTL8812AU Wi-Fi adapter only (for now).

### Usage
1. Download [Zadig](https://zadig.akeo.ie/)
2. Install the libusb driver for your adapter.
   Go *Options* → *List All Devices*.
   ![](tutorials/zadig1.jpg)
   Select your adapter. Install the driver. Remember the USB ID, we will need it soon.
   ![](tutorials/zadig2.jpg)

3. Select the adapter with the previously obtained USB ID.
4. Select your drone channel.
5. Select your WFB key.
6. *Start* & Fly!

### Common run issues

If the application crashes at startup, try installing [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170#latest-microsoft-visual-c-redistributable-version).

### Latency test
![](tutorials/latency_test.jpg)

### TODOs
- Ground side OSD
- Night image enhancement
- Linux client
- Android client

### How to build
1. Install vcpkg.
   ```powershell
   git clone https://github.com/microsoft/vcpkg.git
   cd vcpkg; .\bootstrap-vcpkg.bat
   ```
   
2. Install dependencies.
   ```powershell
   .\vcpkg integrate install
   .\vcpkg install libusb ffmpeg libsodium opencv
   ```

3. Add VCPKG_ROOT() to environment. (Change the value to your vcpkg path.)
   ![](tutorials/vcpkg.jpg)

4. Clone third-party library source.
   ```powershell
   cd aviateur
   git submodule init
   git submodule update
   ```

5. Open as a CMake project and build.

### Common build issues

```
CMake Error at C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/share/cmake-3.29/Modules/FindPackageHandleStandardArgs.cmake:230 (message): ...
```

This is because the pre-installed vcpkg from Visual Studio installer overrides the PKG_ROOT environment variable.
To fix this, find `set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")` in CMakeLists.txt, replace `$ENV{VCPKG_ROOT}` with the vcpkg you cloned previously.
