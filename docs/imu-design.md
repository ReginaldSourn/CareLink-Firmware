# CareLink IMU architecture

Status: implemented for **LSM6DSO (letter O)** with I2C polling and Vext power
selected by default when enabled. **LSM6DS0 (zero)** is a different device and
is not supported. IMU support defaults off; SDA/SCL and address strapping still
need confirmation. See the README for configuration and bench checks.

## Layout

```text
components/
  carelink_imu/
    CMakeLists.txt
    Kconfig                      # Enable, rate, ranges, freshness limit
    include/
      carelink_imu.h             # Public start and snapshot API
      carelink_imu_state.h       # Sample, health and freshness types
    carelink_imu.c               # Sampling task, locking, startup/cleanup
    carelink_imu_state.c         # Pure C freshness/state classification
    lsm6dso_device.h             # Private device interface
    lsm6dso_device.c             # Identity, reset, configuration, sample reads
    lsm6dso_port.h               # Private transport context
    lsm6dso_port.c               # ESP-IDF I2C callbacks for ST driver
    vendor/
      lsm6dso_reg.h              # ST platform-independent register driver
      lsm6dso_reg.c
      LICENSE                   # Preserve upstream license
      README.md                 # Source URL and pinned upstream revision
  carelink_i2c/                  # Owns OLED bus; optional IMU sharing
    CMakeLists.txt
    include/carelink_i2c.h       # Initialize once and obtain shared bus handle
    carelink_i2c.c               # Own bus for firmware lifetime
  carelink_board/                # Pin/power configuration and validation
  carelink_oled/                 # Uses shared I2C0 bus
main/
  main.c                        # Start services; consume IMU snapshot
tests/
  test_imu_state.c               # No-data, fresh, stale and fault transitions
  test_lsm6dso_device.c           # Mock transport and device failure cases
  test_imu_port.c                # Vext, cleanup and shared-bus lifetime
  imu_mocks/                    # Minimal host ESP-IDF transport doubles
docs/
  imu-design.md
  hardware.md                   # Hardware decisions and outstanding checks
```

`carelink_imu` is the application-facing name so consumers do not depend on one
sensor model. The `lsm6dso_*` and vendor headers are private. The device layer
accepts transport callbacks so host tests can simulate registers without
ESP-IDF. ESP-IDF calls stay in the service and port layers.

## API and data contract

Follow the existing GPS service pattern:

```c
esp_err_t carelink_imu_start(void);
esp_err_t carelink_imu_get_snapshot(carelink_imu_snapshot_t *snapshot);
```

The snapshot contains acceleration in m/s², angular velocity in rad/s,
monotonic sample reception time in microseconds, a sample sequence number,
health state and read-error count. Axes use the sensor frame; the physical
mounting must be documented before introducing a body-frame transform. States are
`NO_DATA`, `READY`, `STALE` and `ERROR`. Only `READY` permits using the values.
Freshness is classified when reading the snapshot, so a stopped sampling task cannot
leave an old sample marked ready. A failed read invalidates the current sample;
only a complete successful acquisition refreshes its timestamp.

Start once from `app_main`; a duplicate successful start returns an invalid-state
error. Failed startup releases owned resources and allows retry. The service
runs for firmware lifetime and provides synchronized snapshot copies. When
disabled in Kconfig, its entry points return `ESP_ERR_NOT_SUPPORTED`.

The sampling task owns all sensor operations. It polls data-ready independently
of the one-second application logging loop. Both sensors run at the selected
rate with block data update and register auto-increment enabled. Readiness and block
data update do not by themselves guarantee precisely synchronized accel/gyro
timestamps. A latest-sample snapshot suits diagnostics; future motion detection
that requires every sample needs a bounded stream with explicit overflow counts.

## Bus and board integration

The OLED component obtains I2C0 from `carelink_i2c`. If the IMU uses the same
wires, it obtains that same bus. Each device owns its device handle, while the
shared bus remains alive for firmware lifetime.
OLED startup cleanup releases only OLED resources. A runtime display failure
resets the panel and retains its device until reboot to avoid removing it
during an IMU transfer. An OLED failure cannot remove the IMU's bus.
Startup runs serially from `app_main`; bus acquisition
and service startup are not concurrent APIs. Transfers use finite 100 ms
timeouts. Shared Vext stays powered for firmware lifetime.

If the IMU uses separate header wires, its port owns I2C1 and releases it on
failed startup. SDA/SCL default to `-1` until configured. Physical pin and power
choices live in `carelink_board`, which checks GPS collisions and reserved
pins. Sensor rate, range and freshness policy live in the IMU Kconfig. SPI,
interrupts and FIFO support remain future work.

## Startup, recovery and verification

Startup validates the pins, enables Vext, adds the device handle and checks
identity. It resets the sensor with a 200 ms polling deadline (each transfer
also has a finite timeout), sets ranges/ODRs and waits for settling before
starting the task. Failed startup releases owned bus/device resources; a task
creation failure additionally attempts to power down the sensor. A mismatched
identity is rejected before writing any registers.

A runtime read failure marks the snapshot faulty immediately. The task retries
initialization after a one-second delay, retaining the old sample as unusable
until a complete new sample arrives. A lack of data-ready progress for the
freshness interval also triggers reinitialization. Recovery does not switch off
Vext. The task does not retry failed initial startup; the caller may retry it.

Host tests cover state expiry, all configured scales/rates, wrong identity,
reset timeout, each startup transfer failure, partial read failures, retry and
port resource cleanup. Firmware build checks cover enabled and disabled IMU
configurations. Confirm the physical part, breakout, bus, address and pin
mapping before bench tests for orientation, stationary readings,
disconnect/recovery and simultaneous OLED/GPS/IMU operation.

Fall detection and SOS policy belong in a later application component consuming
IMU samples; they should not be embedded in the sensor register driver.

## Sources

- [ST LSM6DSO product page](https://www.st.com/en/mems-and-sensors/lsm6dso.html)
- [ST LSM6DS0 product page](https://www.st.com/en/mems-and-sensors/lsm6ds0.html)
- [ST LSM6DSO platform-independent C driver](https://github.com/STMicroelectronics/lsm6dso-pid)
- Existing project patterns: `carelink_gps`, `carelink_board`, `carelink_oled`
  and `tests/CMakeLists.txt`.
