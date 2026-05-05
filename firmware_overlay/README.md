# Firmware Overlay

This folder contains the minimal firmware-side pieces required for automatic RGB backlight restore after reboot.

## How to use

1. Open a clean official `flipperzero-firmware` checkout.
2. Copy `lib/drivers/*` from this folder into the firmware tree.
3. Apply `official_firmware_rgb_backlight_boot.patch` from the firmware root.
4. Build the firmware with standard `fbt`.

## Files included

- `official_firmware_rgb_backlight_boot.patch`
- `lib/drivers/rgb_backlight.c`
- `lib/drivers/rgb_backlight.h`
- `lib/drivers/rgb_backlight_filename.h`

## Note

The external `.fap` and the firmware overlay use the same settings storage format and path:

```c
INT_PATH(".rgb_backlight.settings")
```

That means you can configure settings from the `.fap`, then boot into firmware with this overlay and keep the same colors on startup.

## Apply patch

From the official firmware root:

```powershell
git apply path\to\official_firmware_rgb_backlight_boot.patch
```
