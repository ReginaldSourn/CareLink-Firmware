# CareLink secure BLE mobile configuration

CareLink uses ESP-IDF Local Control over BLE with Protocomm Security 2
(SRP-6a). The feature is disabled by default. When enabled, the pendant
advertises as `CareLink-XXXXXX`, where the suffix is derived from its Bluetooth
MAC address. The default configuration window is five minutes after every boot;
set the window to zero only when an always-on BLE service and its battery cost
are acceptable.

Each manufactured pendant needs its own 1-32 byte Security 2 username and
12-63 byte password. Deliver these credentials to the authorized mobile client
out of band. They are not the LoRa HMAC key and must never be reused as that key.
Production devices should also enable secure boot, flash encryption and NVS
encryption because compiled onboarding credentials and saved settings are not
protected at rest by this component alone.

## Local Control contract

- BLE service UUID bytes: `4c 68 8a f2 38 47 4e b1 99 4b 21 66 43 4c 01 00`
- Protocomm security: version 2
- Property name: `carelink_config`
- Property type: `1`
- Property access: read/write
- Property size: exactly 28 bytes

The mobile application should use ESP-IDF Protocomm/Local Control framing. It
must establish a Security 2 session before reading or writing the property. A
valid write is committed to the inactive NVS slot, verified by the firmware on
the next boot, and followed by an automatic restart after approximately 1.5
seconds. The client should treat connection loss after a successful response as
the expected restart.

Writes are rejected while an SOS is arming, active, or has an unacknowledged
cancellation. This prevents a mobile configuration restart or frequency change
from interrupting emergency delivery. If an SOS starts during the short restart
delay, the restart is postponed and the saved settings take effect on the next
normal reboot. Reading the current value remains safe.

## Version 1 value

All multibyte integers are unsigned little-endian. Reserved bytes must be zero.

| Offset | Size | Field | Accepted values |
| ---: | ---: | --- | --- |
| 0 | 1 | version | `1` |
| 1 | 1 | flags | bit 0: OLED enabled; all other bits zero |
| 2 | 1 | LoRa TX power | signed two's-complement, -9 to +14 dBm |
| 3 | 1 | maximum transmissions | 1-100, including initial transmission |
| 4 | 4 | LoRa frequency Hz | supported SX1262 calibration bands below |
| 8 | 4 | first retry delay ms | 500-60000 |
| 12 | 4 | maximum retry delay ms | first delay through 3600000 |
| 16 | 4 | location update interval ms | 10000-3600000 |
| 20 | 4 | GNSS freshness limit ms | 1000-60000 |
| 24 | 4 | reserved | zero |

Accepted frequency bands are 430-440 MHz, 470-510 MHz, 779-787 MHz,
863-870 MHz and 902-928 MHz. Firmware acceptance only means the SX1262 can be
calibrated for the value. The mobile product must enforce the deployment
country's permitted frequency, power, duty cycle and channel plan.

The read response returns the settings applied during the current boot. A write
is all-or-nothing: wrong size/version, unknown flags, nonzero reserved bytes,
unsupported frequency, or an out-of-range field rejects the entire value and
does not change NVS.

## Deliberately excluded settings

The mobile interface cannot change the PCB/RF-front-end profile, GPIOs, button
polarity or pulls, GNSS UART wiring, IMU wiring, flash layout, BLE credentials,
or the device LoRa HMAC key. Those values can electrically damage hardware or
change the device security identity and remain manufacturing/firmware inputs.

Runtime settings are stored as two explicit 44-byte NVS records with schema,
generation and CRC32. Firmware selects the newest valid generation with
wrap-safe comparison. Missing records use firmware defaults; two corrupt
records trigger recovery defaults and a warning instead of applying partial
data.
