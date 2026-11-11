# Heltec V4 and L76C integration research

Checked on 2026-09-24. Board and receiver details still need verification
against the physical unit before flashing.

## Board selection

Heltec identifies the V4 as ESP32-S3 with SX1262, 16 MB flash and an eight-pin
GNSS interface. The original R2 board has 2 MB PSRAM and no CP2102 USB bridge.
The original CareLink README's `esp32c3` target is therefore inappropriate for
this test. The firmware selects ESP32-S3 and native USB Serial/JTAG. Sources:
[Heltec V4 overview](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v4/),
[Espressif USB console guide](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/api-guides/usb-serial-jtag-console.html).

On R8, GNSS power control moves from GPIO34 to GPIO42, GNSS reset is removed,
and Vext moves from GPIO36 to GPIO40. Board selection is explicit so the driver
does not apply R2 reset/wake settings to R8. See
[Heltec's R8 changes](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v4_r8_board/).

The R2 V4.3 schematic connects VBAT to GPIO1/ADC1 channel 0 through a switched
390 kΩ/100 kΩ divider. GPIO37 (`ADC_Ctrl`) drives an NPN that pulls down the
gate of the P-channel high-side switch, so GPIO37 high enables measurement and
low disables the divider. Firmware uses calibrated ADC readings and a 4.9
divider multiplier. This measures terminal voltage only; there is no fuel-gauge
or coulomb-counter IC. The R8 pin map removes `ADC_Ctrl`, so this exact path is
compiled only for the R2 board selection.

## Signal direction and power

The [V4.2 datasheet](https://s.heltec.cn/download/WiFi_LoRa_32_V4/datasheet/WiFi_LoRa_32_V4.2.0.pdf)
labels GPIO39 `GNSS_TX` and GPIO38 `GNSS_RX`. These describe the receiver side:
MCU UART RX uses GPIO39. On R2, GPIO41 is PPS, GPIO40 wake and GPIO42 reset.

The [V4.3 schematic](https://resource.heltec.cn/download/WiFi_LoRa_32_V4/Schematic/HTIT-WB32LAF_V4.3.pdf)
was inspected visually. `TX_GPS` routes to GPIO39, `RX_GPS` to GPIO38, and
`VGNSS_Ctrl` to GPIO34. Q7 is a high-side P-channel switch with a gate pull-up:
pulling the control low enables GNSS power. This supply is separate from OLED
Vext; GPIO36 is not the GNSS enable.

Upstream [Meshtastic board definitions](https://github.com/meshtastic/firmware/blob/master/variants/esp32s3/heltec_v4/variant.h)
also assign GPS RX 39, TX 38 and active-low enable 34. Adjacent comments describe
directions inconsistently; the mapping above follows the manufacturer's
module-side net labels.

V4.2 and V4.3 use different radio front ends, GC1109 and KCT8103L. A future
CareLink radio driver must account for the revision and regional settings.
This GPS milestone does not initialize or transmit through SX1262. See
[Heltec's FEM guidance](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v4/).

## L76C is not L76K or LC76G

The requested receiver is L76C. [Heltec's plug-in accessory](https://heltec.org/project/l76-gnss-module/)
is L76K, so electrical/socket compatibility cannot be inferred from “L76.”
Quectel's [GNSS product table](https://market.quectel.com/wp-content/uploads/custom/pdf/Quectel_Product_Brochure_CN_V6.7.pdf)
lists L76C and L76C R2.0 separately from L76K and gives 9600 baud as their
default. The UART setting is configurable for a reconfigured receiver.

Quectel's [L26C/L76C-family protocol specification](https://www.quectel.com.cn/download/quectel_l26cl26c_r3-0l76cl76c_r3-0_gnss_%E5%8D%8F%E8%AE%AE%E8%A7%84%E8%8C%83_v1-1)
requires login; its full content was not available for this research. Exact
L76C revision, breakout supply/logic levels and reset/wake requirements remain
unverified. No PMTK, PCAS, PAIR, baud-change or reset commands are assumed
compatible or sent. Standard checksummed RMC/GGA reception provides an initial
integration without changing receiver configuration.

## Validation boundary

### OLED integration

The [Heltec V4.2 datasheet](https://s.heltec.cn/download/WiFi_LoRa_32_V4/datasheet/WiFi_LoRa_32_V4.2.0.pdf)
identifies a 128x64 SSD1315 OLED with SDA GPIO17, SCL GPIO18 and reset GPIO21.
Its supply is Vext (active low GPIO36 on R2, GPIO40 on R8). Heltec's
[display implementation](https://github.com/HelTecAutomation/Heltec_ESP32/blob/master/src/HT_SSD1306Wire.h)
uses the SSD1306 command set, horizontal addressing and page-ordered pixels.
CareLink uses the built-in ESP-IDF panel driver, with Heltec's clock, precharge,
VCOM and default orientation settings. See
[Espressif's I2C display documentation](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32/api-reference/peripherals/lcd/i2c_lcd.html).
The code is built against the installed ESP-IDF v6.1 API, including a finite
100 ms I2C transaction timeout. Live display brightness/orientation still require
checking on the physical board.

### LSM6DSO IMU integration

The IMU implementation targets LSM6DSO (letter O), using ST's
[platform-independent driver](https://github.com/STMicroelectronics/lsm6dso-pid)
at the revision recorded in `components/carelink_imu/vendor/README.md`.
The user selected Vext supply; its R2/R8 control is shared with the OLED and
must remain enabled during IMU recovery. SDA/SCL and breakout details have not
been supplied. IMU support therefore defaults disabled and dedicated-bus GPIOs
default to `-1`. An explicit shared-bus option uses OLED SDA17/SCL18; otherwise
the driver uses I2C1 on validated header pins. The address is configurable as
7-bit `0x6A` or `0x6B`. Confirm CS/SA0 strapping, supply compatibility and proper
pull-ups against the breakout schematic before enabling it. LSM6DS0 (zero)
requires a different driver and is not supported by this implementation.

### Physical testing status

Compilation and native tests are possible locally. No connected serial device
is available for flashing or reception testing. Confirm R2/R8, module marking,
antenna, UART levels and socket/jumper wiring, then run the README's bench test.
L76K accessory supply limits must not be reused as verified L76C limits.
