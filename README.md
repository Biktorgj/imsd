# This project has been moved and no further code will be commited here.
Please go to the new location at [PostmarketOS](https://gitlab.postmarketos.org/modem/openimsd/openimsd)

# IMSD

A small(ish?) daemon to control IMS on Qualcomm Modems via QMI / QRTR.

IMSD's job is to configure the baseband inside a Qualcomm phone so it is able to establish an IMS session with the carrier, to allow VoLTE calls and messages to work. To accomplish this, imsd relies in libqmi to handle the actual communication with the baseband.

## Dependencies:
- glib
- libqmi
- libqrtr

## Supported communication channels:
- `QMI` (no functional DCM server here)
- `QRTR` (the tool is being developed on a qrtr based phone)

## Building
You'll need `meson`, `ninja`, `gcc`/`musl` and glibc, libqmi, libqrtr and related dev packages for this to work (check meson.build for a complete list of dependencies).

1. `meson setup build`
2. `ninja -C build`
3. Check if it works: `./build/imsd --device=qrtr://0`

## Roadmap
* [ ] QMI Client Initialization
  * [X] Network Access Service
  * [X] Wireless Data Service
  * [X] Persistent Device Config Service
  * [ ] Modem Filesystem Service
  * [X] IMS Settings Service (libqmi calls it IMS)
  * [X] IMS Application Service
  * [ ] Voice Service
* [X] QRTR Server initialization
  * [X] DCM Server
* [ ] Retrieval of samples and data packets for undocumented services
  * [ ] Modem Filesystem Service (WIP)
  * [X] DCM Server (partial, so far only what we've seen)
  * [ ] Persistent Device Configuration Service (partial support exists in QMI)
* [X] IMS Registration
* [X] IMS status tracking (SIM 0)
  * [ ] IMS Status tracking (multi SIM)
* [ ] WDS: Support for multiple IMS sessions in multiple SIMs
  * [X] IPv4
  * [ ] IPv6
* [X] DCM: SIM slot dependent session bringup
* [X] IMS Call capability
* [X] SMS over IMS
* [ ] Session recovery on network loss
* [ ] RAT based activation (don't try to bringup IMS over an unsupported network)
* [ ] NAS: SIM based network availability check (tracking of multiple SIM service)
* [ ] SIM dependent PDC
* [ ] Automatic PDC selection based on SIM (on multiple SIMs)
  * [ ] Graceful service restart on PDC change
* [ ] RAW IMS configuration parameter passing when no suitable PDC is available
  * [ ] Per carrier config file handling (Modem Filesystem)
* [ ] Voice Mixer controls (in call audio)
  * [ ] Device based config files (specific audio mixer controls for specific devices)
