# Receive H265

```bash
gst-launch-1.0 -vvv udpsrc port=5600 caps='application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H265' ! rtpjitterbuffer ! rtph265depay ! avdec_h265 ! glimagesink sync=false
```

# Receive H264

```bash
gst-launch-1.0 -vvv udpsrc port=5600 caps='application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264' ! rtpjitterbuffer ! rtph264depay ! avdec_h264 ! glimagesink sync=false
```
