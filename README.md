# IMSD

A small daemon to control IMS on Qualcomm Modems via QMI / QRTR.

IMSD's job is to configure the baseband inside a Qualcomm phone so it is able to establish an IMS session with the carrier, to allow VoLTE calls and messages to work. To accomplish this, imsd relies in libqmi to handle the actual communication with the baseband.

### How does it work (mostly)
```mermaid
graph
A[IMSD] --> B(NAS service connect)
B(NAS service connect) --> C(Get MCC MNC)
C(Get MCC MNC) --> D(Find carrier in file)
D(Find carrier in file) --> E(Set IMS settings in baseband)
E(Set IMS settings in baseband) --> F(Start WDS)
F(Start WDS) --> G(Setup indications)
G(Setup indications) --> H(Wait for events)
```
### Supported communication channels:
- QMI
- QRTR

### Building
You'll need `meson`, `ninja`, `gcc`/`musl` and glibc, libqmi, libqrtr and related dev packages for this to work.

1. `meson setup build`
2. `ninja -C build`
3. Check if it works: `./build/imsd --device=qrtr://0`


```mermaid
graph LR
A[IMSD] --> libqmi((libqmi)) --> B[Baseband]
B[Baseband] --> C(ims_settings_svc)
B[Baseband] --> D(ims_rtp_svc)
B[Baseband] --> E(ims_presence_svc)
B[Baseband] --> F(ims_video_telephony_svc)
```
