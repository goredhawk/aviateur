Refer to https://learn.microsoft.com/en-us/windows/wsl/connect-usb first.

```bash
usbipd list
```

```bash
usbipd bind --busid 3-4
```

```bash
usbipd attach --wsl --busid 3-4
```
