# Aviateur

Cross-platform OpenIPC FPV ground station. Forked from [fpv4win](https://github.com/OpenIPC/fpv4win]).

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
6. *Start* & Enjoy!

### Latency Test
![](tutorials/latency_test.jpg)

### Todo
- Linux client
- Android client
- Hardware acceleration decoding

### How to build
1. Install vcpkg.
   ```powershell
   git clone https://github.com/microsoft/vcpkg.git
   cd vcpkg; .\bootstrap-vcpkg.bat
   ```
   
2. Install dependencies.
   ```powershell
   .\vcpkg integrate install
   .\vcpkg install libusb ffmpeg vcpkg-tool-ninja
   ```

3. Add VCPKG_ROOT() to environment. (Change the value to your vcpkg path.)
   ![](tutorials/vcpkg.jpg)

4. Clone third-party library source.
   ```powershell
   git submodule init
   git submodule update
   ```

5. Open as a CMake project and build.
