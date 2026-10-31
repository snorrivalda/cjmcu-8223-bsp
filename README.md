CJMCU-8223 board support package + brew pill
=============================================

This repository holds two things for the CJMCU-8223:

* `hw/bsp/cjmcu-8223` – an [Apache Mynewt](https://mynewt.apache.org/) board
  support package for the board.
* `apps/brewpill` – a **brew pill**: a floating BLE hydrometer that measures
  the specific gravity (SG) of beer while it ferments.

The board
---------

The MCU is an nRF51822 (QFAA: 16 kB RAM, 256 kB flash). The board also has
an LIS3DH accelerometer.

| Signal      | nRF51 pin |
|-------------|-----------|
| LIS3DH SCL  | P0.07     |
| LIS3DH SDA  | P0.06     |
| LIS3DH INT1 | P0.01     |
| LED         | P0.23 (see note) |

The LIS3DH sits on I2C address `0x19` (SA0 high). These pins come from
existing CJMCU-8223 projects, not from an official schematic.

This board is inexpensive but has some shortcomings:

* On some boards the LED is connected to the accelerometer, so it cannot be
  driven from a GPIO alone.
* The board does not come with the Nordic SoftDevice flashed. That is fine:
  the brew pill uses NimBLE, Mynewt's own Bluetooth stack, so the SoftDevice
  is not needed.

How the brew pill works
-----------------------

The board, the batteries and some ballast go into a sealed tube that floats
in the fermenter. Denser liquid lifts the tube higher, so the tube floats at
a steeper angle. As sugar is fermented into alcohol, the gravity drops and
the tube tilts further over.

Every 60 seconds the pill:

1. averages 50 accelerometer samples (1 second at 50 Hz) and computes the
   tilt of the tube from vertical;
2. converts the tilt to SG with a calibration polynomial (optional);
3. reads the temperature (nRF51 internal sensor) and the battery voltage;
4. puts everything in a BLE advertisement, sent once per second at +4 dBm.

The pill is broadcast-only. There is no pairing and no connection: any
phone scanner app, PC, Mac or Raspberry Pi in range can pick up the
readings. Between readings the accelerometer is powered down and the CPU
sleeps.

Hardware build
--------------

* **Power:** 2×AA or 2×AAA cells in series (2.0–3.2 V), wired straight to the
  board's 3.3 V/VCC pin. Bypass any on-board regulator: the nRF51 runs from
  1.8–3.6 V. Don't use a supply above 3.6 V. The batteries double as
  ballast. The battery voltage is reported in every reading.
* **Tube:** use a food-safe, watertight tube, such as a PET preform tube.
  Mount the board flat along the length of the tube, with the batteries at
  the bottom.
* **Ballast:** add weight at the bottom until the pill floats at roughly
  20–30° from vertical in plain water. Below that it can't show gravity
  changes well. Heavier wort will push it towards vertical, and it leans
  over further as the beer ferments out.
* **Radio:** 2.4 GHz is absorbed strongly by liquid and blocked completely by
  stainless steel. A plastic fermenter with the receiver within a few metres
  works best.

Building and flashing
---------------------

Install [newt](https://mynewt.apache.org/latest/get_started/native_install/)
and the `arm-none-eabi-gcc` toolchain, then create a project:

```sh
newt new brewpill-project
cd brewpill-project
```

Add this repository to `project.yml`:

```yaml
project.repositories:
    - apache-mynewt-core
    - cjmcu-8223-bsp

repository.cjmcu-8223-bsp:
    type: github
    vers: 0-dev
    user: snorrivalda
    repo: cjmcu-8223-bsp
```

Fetch the repositories and create the bootloader and app targets:

```sh
newt upgrade

newt target create boot
newt target set boot app=@mcuboot/boot/mynewt \
    bsp=@cjmcu-8223-bsp/hw/bsp/cjmcu-8223 build_profile=optimized

newt target create pill
newt target set pill app=@cjmcu-8223-bsp/apps/brewpill \
    bsp=@cjmcu-8223-bsp/hw/bsp/cjmcu-8223 build_profile=optimized
```

Connect an SWD probe to the board (SWDIO, SWCLK, GND, VCC). The BSP uses a
J-Link through OpenOCD. For an ST-Link or CMSIS-DAP probe, change the
`interface` line in `hw/bsp/cjmcu-8223/openocd.cfg`. Then build and flash:

```sh
newt build boot && newt load boot           # once
newt build pill && newt create-image pill 1.0.0 && newt load pill
```

At boot the LED blinks three times if it is wired to P0.23.

Receiving readings
------------------

`tools/brewpill_receiver.py` listens for the pill on Linux (including a
Raspberry Pi), macOS or Windows:

```sh
pip install bleak
python3 tools/brewpill_receiver.py               # print readings
python3 tools/brewpill_receiver.py --csv log.csv # ...and log them
```

You can also use a generic BLE scanner app on a phone (e.g. nRF Connect).
Look for a device called `BrewPill` with manufacturer data for company
`0xFFFF`.

Setting up and calibrating
--------------------------

1. **Find the tube axis.** Hold the pill upright and watch the `accel=`
   values. The axis that reads close to ±1000 mg runs along the tube.
   Set it with `BREWPILL_TUBE_AXIS` (0 = X, the default; 1 = Y; 2 = Z):

   ```sh
   newt target amend pill syscfg=BREWPILL_TUBE_AXIS=1
   ```

   Tilt should then read near 0° upright and near 90° lying flat.

2. **Collect calibration points.** Float the pill in plain water (SG 1.000)
   and in a few sugar solutions, ideally up to about 1.080. Check each one
   with a regular hydrometer or refractometer. Note the tilt for each, for
   example in a file `points.csv`:

   ```
   tilt,sg
   25.1,1.000
   38.4,1.020
   51.0,1.040
   63.9,1.060
   ```

   Keep all the liquids at a similar temperature, near your fermentation
   temperature.

3. **Fit the curve:**

   ```sh
   python3 tools/calibrate.py points.csv
   ```

   This prints the coefficients in two forms. Either paste them into the
   target, so the pill broadcasts SG itself:

   ```sh
   newt target amend pill syscfg=BREWPILL_SG_C0=...:BREWPILL_SG_C1=...:BREWPILL_SG_C2=...:BREWPILL_SG_C3=...
   ```

   or pass them to the receiver with `--poly`, which needs no reflashing.

Until it is calibrated, the pill broadcasts only the tilt and marks SG as
uncalibrated.

### Settings (`apps/brewpill/syscfg.yml`)

| Setting | Default | Meaning |
|---|---|---|
| `BREWPILL_MEAS_INTERVAL_S` | 60 | Seconds between readings |
| `BREWPILL_ADV_INTERVAL_MS` | 1000 | Advertising interval |
| `BREWPILL_ACCEL_SAMPLES` | 50 | Samples averaged per reading (50 Hz) |
| `BREWPILL_TUBE_AXIS` | 0 | Accelerometer axis along the tube |
| `BREWPILL_SG_C0`…`C3` | 0 | Calibration: SG = C0 + C1·a + C2·a² + C3·a³ |
| `BREWPILL_TEMP_OFFSET_C100` | 0 | Temperature correction in 0.01 °C |
| `BREWPILL_NAME` | `"BrewPill"` | Name in the scan response |
| `BREWPILL_BOOT_BLINK` | 1 | Blink the LED at boot |

**Temperature:** the nRF51's internal sensor is only accurate to about
±4 °C before correction. Compare it once against a thermometer and set
`BREWPILL_TEMP_OFFSET_C100` to the difference. For example, if the pill
reads 1.5 °C too high, set it to `-150`.

BLE payload format
------------------

The pill sends manufacturer-specific data, company ID `0xFFFF`. All fields
are little-endian:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Company ID `0xFFFF` |
| 2 | 2 | Magic `"BP"` |
| 4 | 1 | Payload version (`1`) |
| 5 | 1 | Status flags (see below) |
| 6 | 1 | Reading sequence number (wraps) |
| 7 | 2 | Tilt from vertical, 0.01° (uint16) |
| 9 | 2 | SG × 10000 (uint16), `0` = not calibrated |
| 11 | 2 | Temperature, 0.01 °C (int16) |
| 13 | 2 | Battery voltage, mV (uint16) |
| 15 | 6 | Raw acceleration X, Y, Z in mg (3 × int16) |

The status flags are:

* `0x01` – the accelerometer is not responding.
* `0x02` – SG is not calibrated.
* `0x04` – the accelerometer was found at the alternate address `0x18`.

BSP notes
---------

The BSP targets current mynewt-core. It uses the core's common nRF51
startup code and `sbrk`, and flashes through the core's OpenOCD helpers, so
the bootloader and image slots land at the right offsets.

Flash layout:

| Area | Offset | Size |
|---|---|---|
| Bootloader (MCUboot) | `0x00000` | 16 kB |
| Reboot log | `0x04000` | 16 kB |
| Image slot 0 | `0x08000` | 110 kB |
| Image slot 1 | `0x23800` | 110 kB |
| Scratch | `0x3f000` | 2 kB |
| NFFS | `0x3f800` | 2 kB |

The BSP assumes the board has a 32.768 kHz crystal (`MCU_LFCLK_SOURCE:
LFXO`). If your board has none, it will hang at start-up waiting for the
clock. In that case set `MCU_LFCLK_SOURCE: LFSYNTH`. That works but uses
more power. `LFRC` is not allowed together with the BLE controller. The
DC/DC converter is left off.
