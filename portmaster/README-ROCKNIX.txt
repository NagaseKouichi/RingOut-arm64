Ring Out — ROCKNIX / PortMaster (native aarch64)
=================================================

This is a direct ROCKNIX PortMaster integration for the native ARM64 Ring Out
runtime. It uses ROCKNIX's own glibc, Freedreno/Mesa Vulkan driver, Wayland
session, audio stack, and SDL controller mapping. It does not install packages,
write /usr or /lib, or use a private runtime rootfs.

SUPPORTED BASELINE
  - ROCKNIX next/current aarch64 Qualcomm devices with Vulkan support:
    SM8250, SM8550, SM8650, and SM8750.
  - The initial compatibility check used the ROCKNIX SM8550 aarch64 runtime
    built from distribution next (glibc 2.41).
  - This port is not for ARMv7, non-Vulkan devices, or old ROCKNIX builds with
    glibc older than 2.38.

WHAT IS NOT INCLUDED
  This archive does not include a GameCube disc image, extracted game/,
  generated chunks, or g<ID>_recomp.so. Those are derived from your own disc.

INSTALL
  Extract this archive to /storage/roms/ports/ so that the layout is:

    /storage/roms/ports/RingOut.sh
    /storage/roms/ports/RingOut/RingOut
    /storage/roms/ports/RingOut/bin/moderngekko-run

  Launch RingOut.sh from the Ports system / PortMaster.

PREPARE YOUR OWN GAME
  1. On a compatible ARM64 Linux machine, use RingOut-<version>-linux-aarch64.zip
     to run setup.sh against your own disc image. For a target-native profile:

       ./setup.sh --pgo /path/to/your-disc.iso

     Do not use --deck: it is an x86_64 Steam Deck option.

  2. Copy the generated files into this port directory:

       game/                  -> RingOut/game/
       bin/g<ID>_recomp.so    -> RingOut/bin/

  The module must be an ARM aarch64 ELF built with the same Ring Out release as
  this runtime.

CONTROLLER AND DISPLAY
  RingOut.sh sources ROCKNIX's PortMaster control.txt and calls get_controls,
  preserving SDL_GAMECONTROLLERCONFIG_FILE from EmulationStation/InputPlumber.
  It then removes the old PortMaster compat-library priority so RingOut loads
  current ROCKNIX glibc, Freedreno/Mesa Vulkan, and Wayland libraries. The
  launcher requests a Sway fullscreen window for moderngekko-run.

TROUBLESHOOTING
  - The first failed launch is logged in RingOut/userdata/rocknix-launch.log.
  - Ensure the selected ROCKNIX device has a working Vulkan ICD. This port uses
    the host driver; do not copy an external Mesa or Vulkan driver into RingOut.
  - If extraction lost modes, run once over SSH:

      chmod +x /storage/roms/ports/RingOut.sh \
        /storage/roms/ports/RingOut/RingOut \
        /storage/roms/ports/RingOut/bin/moderngekko-run

  - A real-device test still needs to confirm picture, audio, physical
    controller input, and an in-game match. A process launch alone is not proof
    of PortMaster compatibility.
