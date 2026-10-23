# CareLink Firmware

ESP-IDF firmware for testing a Quectel L76C GNSS receiver with a **Heltec WiFi
LoRa 32 V4 (ESP32-S3)**. This milestone receives GPS data and reports position
and receiver health over USB and the onboard OLED. Optional LSM6DSO IMU support
reports acceleration, angular velocity and sensor health over USB. LoRa
transport, SOS detection and low power scheduling are future application work.

## Hardware setup

Defaults assume the **V4 R2 board and its GNSS socket**. The exact L76C
breakout, wiring and board revision have not been confirmed. Select R8 in
`menuconfig` if your board is marked ESP32-S3R8; its power control pin differs.
See [hardware research and sources](docs/hardware.md).

| Signal | Default connection |
| --- | --- |
| GNSS TX → ESP32 RX | GPIO39, UART1, 9600 baud, 8N1 |
| GNSS RX | Unconnected for this receive-only test (socket net is GPIO38) |
| GNSS ground | Heltec GND |
| GNSS supply | Compatible socket supply, or the breakout's specified supply |
| GNSS socket power enable | GPIO34 low on R2; GPIO42 low on R8 |

For jumper wiring, use a breakout with compatible supply and UART logic levels.
Check its schematic before powering it; a bare L76C and Heltec's L76K accessory
are different products. ESP32 GPIOs are not 5 V tolerant. A receiver with lower
voltage UART output may require level translation. The firmware does not drive
the receiver's RX, reset or wake pins. A breakout requiring external reset/wake
biasing must provide it according to its own documentation.

For a separately powered breakout, disable **CareLink board → Enable power on
the Heltec GNSS socket**. Connect TX to GPIO39 (or change the RX setting). The
allowed alternate RX pins are 4, 6, 15, 16, 38, 39, 47 and 48; these must also be
unused by your attached expansion hardware. Pin validation excludes onboard
radio, display, memory, USB, strapping and power-control pins.

## Build and flash

Use an activated ESP-IDF environment. The project was built with **ESP-IDF
v6.1**. It uses native ESP-IDF peripheral drivers and a vendored, pinned ST
LSM6DSO register driver; building requires no sensor-library downloads. PSRAM
is not required or enabled.

```sh
# Activate your installed ESP-IDF environment first.
idf.py -B build/heltec-v4 set-target esp32s3
idf.py -B build/heltec-v4 menuconfig
idf.py -B build/heltec-v4 build
idf.py -B build/heltec-v4 -p /dev/ttyACM0 flash monitor
```

Check **Component config → CareLink board** and **CareLink GPS** in menuconfig.
Defaults select R2, GPIO39, 9600 baud, a 5-second freshness limit, 16 MB flash
and native USB Serial/JTAG console. Existing `sdkconfig` values take precedence
over defaults; verify them when reusing an older configuration. Replace the
port name for your system. Hold **PRG**, press/release **RST**, then release
**PRG** if the board does not enter download mode; press RST after flashing if
needed. Exit the monitor with `Ctrl+]`.

## Bench test

1. Connect the correct GNSS antenna and place it outdoors with a clear sky view.
2. Confirm the startup log names the correct board variant and RX pin.
3. Watch the status every second:
   - `no UART data`: check power, ground, RX wiring and module startup.
   - `no valid NMEA`: bytes arrive but framing/checksums fail; check baud and logic levels.
   - `searching`: valid NMEA arrives, but no usable position has been reported.
   - `fix`: current coordinates; GGA may include altitude/satellites/HDOP,
     and RMC may include speed.
   - `stale fix`: the last position report is at least five seconds old.
4. After obtaining a fix, disconnect GNSS TX. The position must expire within
   the configured freshness interval plus the one-second display/logging interval.
5. Reconnect TX and confirm recovery. An explicit RMC `V` or GGA quality `0`
   also invalidates a fix immediately when parsed.

Optional **CareLink GPS → Log received NMEA sentences** helps diagnose module
behavior. Raw logs and normal position logs contain location. Cold acquisition
can take several minutes; a receiver indoors may never fix.

## Onboard OLED

The 128x64 SSD1315 display is enabled by default using ESP-IDF's compatible
SSD1306 driver. I2C0 uses **SDA GPIO17, SCL GPIO18, address 0x3C**, with reset
on **GPIO21**. The board layer enables Vext using GPIO36 on R2 or GPIO40 on R8.
Select the correct variant before flashing. No additional display wiring or
Arduino/LVGL dependency is needed for the onboard screen.

Under **Component config → CareLink OLED**, disable the display for a headless
board or enable **Rotate OLED by 180 degrees** for the opposite orientation.
The screen refreshes once per second with status, signed coordinates, available
altitude or speed, and fix age. GGA reports can show satellite count and HDOP;
RMC reports do not carry those fields. The receiver-health counters appear
while waiting for a fix. Expired/invalid coordinates are removed from the screen.

An absent OLED logs a warning and GPS continues over USB. If a frame transfer
fails, the driver resets the panel to prevent a frozen position remaining
visible and stops updates until reboot. It retains its device resources to
avoid removing a device during an IMU transfer on the shared bus. Vext stays
enabled because it also powers external headers. Check the board variant
and display connector if the screen is blank. `carelink_i2c` owns I2C0 for
firmware lifetime so the OLED and an optional IMU can share the bus safely.

## LSM6DSO IMU

Support targets **LSM6DSO (letter O)**, not LSM6DS0 (zero). It checks the
device identity before configuring it. IMU support is **disabled by default**
until SDA/SCL wiring is selected; no header pin assignment is assumed.
Vext power is the default when enabled, matching the planned hardware.

In `idf.py menuconfig`:

1. Under **Component config → CareLink IMU**, enable LSM6DSO and select the
   7-bit address: `0x6A` for SA0 low or `0x6B` for SA0 high (default `0x6A`).
2. Under **CareLink board**, leave **IMU powered from Vext** enabled and select
   the correct R2/R8 variant. Configure either:
   - A dedicated I2C1 bus: set SDA/SCL to your actual header wiring. Available
     candidates are GPIO4, 6, 15, 16, 38, 39, 47 and 48, excluding the configured
     GPS RX pin (default GPIO39). Pins start at `-1`; reserved, duplicate and
     GPS-conflicting assignments fail startup before configuring GPIOs.
   - **IMU shares OLED I2C0** only when wired to SDA GPIO17 and SCL GPIO18.
     This also works with OLED support disabled.
3. Select the sensor rate and ranges. Defaults are **104 Hz, ±4 g, ±500 dps**
   and a **500 ms** freshness limit. Supported rates are 26/52/104/208 Hz.
4. Build, flash and monitor using the commands above.

Connect the compatible breakout supply to Vext and ground to GND. Confirm its
I2C mode, CS/SA0 strapping and pull-ups from its schematic; use proper external
I2C pull-ups for the 400 kHz bus. Vext remains on during IMU cleanup and recovery
because the OLED and other attached devices may also need it. SDA/SCL, address
strapping and breakout voltage compatibility still need physical verification.

The independent sampling task publishes sensor-frame X/Y/Z acceleration in
**m/s²** (including gravity) and angular velocity in **rad/s**. USB diagnostics
print once per second. `carelink_imu_start()` starts the service;
`carelink_imu_get_snapshot()` copies its latest sample and health atomically.
Use values only when `snapshot.state == CARELINK_IMU_READY`. Timestamps are
monotonic reception times, not exact synchronized sensor timestamps. `sequence`
counts published samples and wraps naturally; `read_errors` includes failed
runtime reads and reinitialization attempts. A successful snapshot call alone
does not imply valid data.

After a runtime transfer failure the service immediately invalidates the sample
and retries initialization with a one-second backoff. Missing data becomes
stale at the configured age and triggers reinitialization, including after a
sensor loses its configuration. Recovery requires a new complete sample before
returning to `READY`. Initial startup failure is logged and leaves the service
stopped; startup can be retried by the application or after a reboot. GPS and
IMU startup failures do not stop each other's diagnostics.

This is a latest-sample diagnostic service, without FIFO, interrupt acquisition,
calibration, mounting transforms or fall/SOS detection. RTOS tick resolution,
OLED traffic and scheduling can reduce the effective acquisition rate. A
lossless motion-processing application should add a bounded sample stream/FIFO
and measure timing before depending on every sensor conversion.

Bench check: keep the unit still and expect acceleration magnitude near
9.81 m/s² and gyro values near zero, then rotate each axis and verify the signs.
Disconnect/reconnect the sensor and confirm error/stale data is rejected and
fresh readings recover. Verify simultaneous GPS and OLED operation. No physical
IMU or connected serial device was available for validation here.

## Code layout

```text
components/
  carelink_board/   Heltec variant, pin validation, GNSS supply control
  carelink_nmea/    Allocation-free streaming parser; independent of ESP-IDF
  carelink_gps/     UART task, health counters, synchronized snapshot API
  carelink_i2c/     Shared I2C0 ownership for OLED and optional IMU
  carelink_imu/     LSM6DSO sampling, SI units, health and recovery
  carelink_oled/    OLED initialization, bounded I2C transfers, 1 KB text framebuffer
main/              CareLink startup, diagnostic consumer and GPS screen layout
docs/hardware.md   Manufacturer research and wiring decisions
docs/imu-design.md IMU architecture and integration details
tests/             Native C parser, IMU, transport lifecycle and display tests
sdkconfig.defaults ESP32-S3, flash and USB defaults
```

Start once with `carelink_gps_start()`. Other CareLink tasks can call
`carelink_gps_get_snapshot()` and use coordinates only when
`snapshot.state == CARELINK_GPS_FIXED`. `fix.valid` alone does not check age.
The service owns UART1 for firmware lifetime and has no stop/sleep API yet.
It uses one 4 KB-stack task, a 2 KB RX ring buffer, a bounded UART event queue
and a 128-byte parser buffer. It allocates no memory per sentence.

Each snapshot contains the latest accepted **RMC or GGA sentence**, including
its available-field flags. Fields are not merged across sentences: altitude
from an earlier GGA cannot silently accompany newer RMC coordinates. Timestamps
are monotonic reception times, not UTC. Checksum failures do not refresh a
position. Other valid NMEA sentences update receiver health only. UART errors
discard the partial stream and invalidate the current fix.

The parser accepts two-letter talker IDs (including GP/GN/BD), requires
checksums, converts coordinates to signed degrees and knots to m/s, and excludes
explicit estimated/manual/simulated fixes. No vendor commands or persistent
module settings are used.

## Host tests

```sh
cmake -S tests -B build/host-tests -D CARELINK_SANITIZE=ON
cmake --build build/host-tests
ctest --test-dir build/host-tests --output-on-failure
```

Use GCC or Clang; omit `CARELINK_SANITIZE` if sanitizers are unavailable.
In a traced sandbox where LeakSanitizer reports that ptrace is unsupported,
run `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/host-tests --output-on-failure`.
This is how the suite was verified here; address and undefined-behavior checks
remain enabled. The parser itself performs no heap allocations.
Tests cover known-checksum fixtures, split input, hemispheres, coordinate
bounds, zero coordinates, no-fix reports, malformed numeric fields, corrupt and
overlong input, arbitrary-byte stress, recovery and freshness boundaries.
OLED tests check framebuffer bounds, clipping, character fallback and clearing
old coordinate rows. A rendered sample screen was also inspected locally.
IMU tests use a mocked register transport to check identity rejection, reset
timeout, each startup transfer failure, readiness, signed conversion, scales,
partial-read failures and retries. State tests check freshness boundaries and
fault precedence; port tests check Vext handling, pin-validation failure,
bounded transfers, cleanup and shared-bus retention.
Build/tests do not verify electrical compatibility or live satellite fixes.
