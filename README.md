# flipper-rgbbacklight-control
The app is in beta version, also i didn't test it because i dont use OFW. With any issue write in issues :) 

# Flipper RGB Backlight

Standalone `uFBT` app plus firmware overlay for the Flipper Zero RGB backlight hardware mod used by Momentum-style builds.

## What is included

- `rgb_backlight_settings.fap` source for configuring RGB backlight settings from Flipper.
- `firmware_overlay/` with firmware-side files needed for boot-time restore after reboot.
- GitHub Actions workflow from `uFBT` template for CI builds.

## Important limitation

The `.fap` can configure colors, save settings, and preview the RGB backlight on hardware with the `PA8` SK6805 mod.

Boot-time restore after reboot is **not possible with a `.fap` alone**.
For the backlight to come up automatically after reboot, you must also apply the files from `firmware_overlay/` to an official firmware checkout and build custom firmware.

## Hardware target

- Flipper Zero
- RGB backlight hardware mod compatible with `SK6805`
- Data pin on `PA8`

## App features

- Enable/disable RGB backlight
- Static / Wave / Solid modes
- Per-LED `#RRGGBB` editing
- Rainbow speed
- Rainbow interval
- Rainbow saturation
- Settings saved to `INT_PATH(".rgb_backlight.settings")`

## Build with uFBT


## Firmware overlay

The repo includes a minimal firmware overlay for boot-time restore.

It contains:

- RGB backlight driver files for `lib/drivers/`
- a patch that hooks the driver into boot-time notification/backlight flow

Workflow:

1. copy the files from `firmware_overlay/lib/drivers/` into a clean official `flipperzero-firmware` checkout
2. apply `firmware_overlay/official_firmware_rgb_backlight_boot.patch`
3. build firmware with standard `fbt`

This is enough for the `.fap` settings file to be restored automatically after reboot.

# TODO:
Draw and add the 10x10 .png icon for the app.
Test the app on the OFW
Release the v1.0.0 of the app
Post to Flipper App Store
Fix all issues of the app (if they exist)
