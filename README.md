# Wi-Fi Broadcast FPV client for Windows platform.

Forked from [fpv4win](https://github.com/OpenIPC/fpv4win]), aviateur is an app for Windows that packages multiple components together to decode an H264/H265 video feed broadcast by wfb-ng over the air.

- [devourer](https://github.com/openipc/devourer): A userspace rtl8812au driver initially created by [buldo](https://github.com/buldo) and converted to C by [josephnef](https://github.com/josephnef) .
- [wfb-ng](https://github.com/svpcom/wfb-ng): A library that allows broadcasting the video feed over the air.

Supports RTL8812AU Wi-Fi adapter only.

It is recommended to use with [OpenIPC](https://github.com/OpenIPC).

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
- OSD
- Hardware acceleration decoding
- ~~Record MP4 file~~
- ~~Capture frame to JPG~~
- Stream to RTMP/RTSP/SRT/WHIP server
- Receive multiple video streams using a single adapter
- ONVIF/GB28181/SIP client

### How to build
1. Install vcpkg.
   ```powershell
   git clone https://github.com/microsoft/vcpkg.git
   cd vcpkg; .\bootstrap-vcpkg.bat
   ```
   
2. Install dependencies.
   ```powershell
   .\vcpkg integrate install
   .\vcpkg install libusb libpcap libsodium ffmpeg qt5 vcpkg-tool-ninja
   ```

3. Add VCPKG_ROOT() to environment. (Change the value to your vcpkg path.)
   ![](tutorials/vcpkg.jpg)

4. Clone third-party library source.
   ```powershell
   git submodule init
   git submodule update
   ```

5. Open as a CMake project and build.
