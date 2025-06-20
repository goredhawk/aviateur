# Receive H265

```bash
gst-launch-1.0 -v udpsrc port=5000 caps='application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H265' ! rtpjitterbuffer ! rtph265depay ! decodebin3 ! autovideosink sync=false
```

# Receive H264

```bash
gst-launch-1.0 -v udpsrc port=5000 caps='application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H264' ! rtpjitterbuffer ! rtph264depay ! decodebin3 ! autovideosink sync=false
```
