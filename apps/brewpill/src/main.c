/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*
 * Brew pill: a floating hydrometer for the CJMCU-8223.
 *
 * The pill floats in the wort/beer at an angle that depends on the liquid's
 * density.  Every BREWPILL_MEAS_INTERVAL_S seconds it measures:
 *   - tilt of the tube axis from vertical (LIS3DH accelerometer)
 *   - specific gravity from the calibration polynomial (optional)
 *   - temperature (nRF51 internal sensor)
 *   - battery voltage (nRF51 ADC on VDD)
 * and broadcasts the values in non-connectable BLE advertisements.
 * See README.md for the payload format.
 */

#include <assert.h>
#include <math.h>
#include <string.h>
#include "os/mynewt.h"
#include "hal/hal_gpio.h"
#include "bsp/bsp.h"
#include "nrfx.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "lis3dh.h"

#define BREWPILL_COMPANY_ID     0xffff  /* Bluetooth SIG "testing" ID */
#define BREWPILL_PAYLOAD_VER    1

#define STATUS_ACCEL_ERROR      0x01
#define STATUS_SG_UNCALIBRATED  0x02
#define STATUS_ACCEL_ALT_ADDR   0x04

#define MEAS_TASK_PRIO          (OS_TASK_PRI_LOWEST - 10)
#define MEAS_STACK_SIZE         OS_STACK_ALIGN(256)

struct brewpill_reading {
    uint8_t status;
    uint8_t seq;
    uint16_t tilt_c;            /* 0.01 degrees from vertical */
    uint16_t sg_x10000;         /* specific gravity * 10000, 0 if unknown */
    int16_t temp_c100;          /* 0.01 degrees C */
    uint16_t batt_mv;
    int16_t accel_mg[3];
};

static struct os_task meas_task;
static os_stack_t meas_stack[MEAS_STACK_SIZE];

static volatile bool ble_synced;
static uint8_t own_addr_type;
static uint8_t adv_mfg[21];
static const char brewpill_name[] = MYNEWT_VAL(BREWPILL_NAME);

/* Die temperature in 0.01 C.  Accuracy is about +/-4 C before offset. */
static int16_t
read_temperature(void)
{
    int32_t t;

    /* nRF51 PAN-31: the temperature offset must be loaded manually. */
    *(volatile uint32_t *)0x4000C504 = 0;

    NRF_TEMP->EVENTS_DATARDY = 0;
    NRF_TEMP->TASKS_START = 1;
    while (NRF_TEMP->EVENTS_DATARDY == 0) {
    }
    NRF_TEMP->EVENTS_DATARDY = 0;
    t = NRF_TEMP->TEMP;
    NRF_TEMP->TASKS_STOP = 1;

    /* nRF51 PAN-28: sign-extend the 10-bit result. */
    if (t & 0x200) {
        t |= (int32_t)0xFFFFFC00;
    }
    /* Unit is 0.25 C. */
    return (int16_t)(t * 25 + MYNEWT_VAL(BREWPILL_TEMP_OFFSET_C100));
}

/* Supply voltage in mV, measured as VDD/3 against the 1.2 V bandgap. */
static uint16_t
read_battery_mv(void)
{
    uint32_t result;

    NRF_ADC->CONFIG =
        (ADC_CONFIG_RES_10bit << ADC_CONFIG_RES_Pos) |
        (ADC_CONFIG_INPSEL_SupplyOneThirdPrescaling << ADC_CONFIG_INPSEL_Pos) |
        (ADC_CONFIG_REFSEL_VBG << ADC_CONFIG_REFSEL_Pos) |
        (ADC_CONFIG_PSEL_Disabled << ADC_CONFIG_PSEL_Pos);
    NRF_ADC->ENABLE = ADC_ENABLE_ENABLE_Enabled;
    NRF_ADC->EVENTS_END = 0;
    NRF_ADC->TASKS_START = 1;
    while (NRF_ADC->EVENTS_END == 0) {
    }
    NRF_ADC->EVENTS_END = 0;
    result = NRF_ADC->RESULT;
    NRF_ADC->ENABLE = ADC_ENABLE_ENABLE_Disabled;

    return (uint16_t)((result * 3600) / 1023);
}

/* Angle between the tube axis and vertical, in degrees. */
static float
tilt_degrees(const struct lis3dh_sample *s)
{
    float x = s->x_mg, y = s->y_mg, z = s->z_mg;
    float g = sqrtf(x * x + y * y + z * z);
    float axis;
    float c;

    switch (MYNEWT_VAL(BREWPILL_TUBE_AXIS)) {
    case 0:
        axis = x;
        break;
    case 1:
        axis = y;
        break;
    default:
        axis = z;
        break;
    }

    if (g < 1.0f) {
        return 0.0f;
    }
    c = fabsf(axis) / g;
    if (c > 1.0f) {
        c = 1.0f;
    }
    return acosf(c) * (180.0f / (float)M_PI);
}

static bool
sg_calibrated(void)
{
    return (MYNEWT_VAL(BREWPILL_SG_C0) != 0.0) ||
           (MYNEWT_VAL(BREWPILL_SG_C1) != 0.0) ||
           (MYNEWT_VAL(BREWPILL_SG_C2) != 0.0) ||
           (MYNEWT_VAL(BREWPILL_SG_C3) != 0.0);
}

/* SG = C0 + C1*a + C2*a^2 + C3*a^3, with a = tilt in degrees. */
static float
tilt_to_sg(float a)
{
    return (float)MYNEWT_VAL(BREWPILL_SG_C0) +
           a * ((float)MYNEWT_VAL(BREWPILL_SG_C1) +
           a * ((float)MYNEWT_VAL(BREWPILL_SG_C2) +
           a * (float)MYNEWT_VAL(BREWPILL_SG_C3)));
}

static void
take_reading(struct brewpill_reading *r, int accel_state)
{
    struct lis3dh_sample s;
    float tilt;
    float sg;

    r->seq++;
    r->status = 0;
    memset(r->accel_mg, 0, sizeof(r->accel_mg));
    r->tilt_c = 0;
    r->sg_x10000 = 0;

    if (accel_state < 0 ||
        lis3dh_measure(MYNEWT_VAL(BREWPILL_ACCEL_SAMPLES), &s) != 0) {
        r->status |= STATUS_ACCEL_ERROR;
    } else {
        if (accel_state == 1) {
            r->status |= STATUS_ACCEL_ALT_ADDR;
        }
        r->accel_mg[0] = (int16_t)s.x_mg;
        r->accel_mg[1] = (int16_t)s.y_mg;
        r->accel_mg[2] = (int16_t)s.z_mg;
        tilt = tilt_degrees(&s);
        r->tilt_c = (uint16_t)(tilt * 100.0f + 0.5f);
        if (sg_calibrated()) {
            sg = tilt_to_sg(tilt);
            if (sg > 0.0f && sg < 6.5f) {
                r->sg_x10000 = (uint16_t)(sg * 10000.0f + 0.5f);
            }
        }
    }
    if (!sg_calibrated()) {
        r->status |= STATUS_SG_UNCALIBRATED;
    }

    r->temp_c100 = read_temperature();
    r->batt_mv = read_battery_mv();
}

static void
encode_payload(const struct brewpill_reading *r)
{
    uint8_t *p = adv_mfg;

    put_le16(p, BREWPILL_COMPANY_ID);
    p[2] = 'B';
    p[3] = 'P';
    p[4] = BREWPILL_PAYLOAD_VER;
    p[5] = r->status;
    p[6] = r->seq;
    put_le16(p + 7, r->tilt_c);
    put_le16(p + 9, r->sg_x10000);
    put_le16(p + 11, (uint16_t)r->temp_c100);
    put_le16(p + 13, r->batt_mv);
    put_le16(p + 15, (uint16_t)r->accel_mg[0]);
    put_le16(p + 17, (uint16_t)r->accel_mg[1]);
    put_le16(p + 19, (uint16_t)r->accel_mg[2]);
}

static int
adv_event(struct ble_gap_event *event, void *arg)
{
    return 0;
}

static void
update_advertising(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp;
    struct ble_gap_adv_params params;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.mfg_data = adv_mfg;
    fields.mfg_data_len = sizeof(adv_mfg);
    rc = ble_gap_adv_set_fields(&fields);
    assert(rc == 0);

    if (ble_gap_adv_active()) {
        return;
    }

    /* The name goes in the scan response to keep room in the advert. */
    memset(&rsp, 0, sizeof(rsp));
    rsp.name = (const uint8_t *)brewpill_name;
    rsp.name_len = strlen(brewpill_name);
    rsp.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    assert(rc == 0);

    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_NON;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(MYNEWT_VAL(BREWPILL_ADV_INTERVAL_MS));
    params.itvl_max = params.itvl_min + BLE_GAP_ADV_ITVL_MS(10);

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params,
                           adv_event, NULL);
    assert(rc == 0);
}

static void
on_sync(void)
{
    int rc;

    /* Use the chip's static random address so the pill keeps its identity. */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    assert(rc == 0);

    ble_synced = true;
}

static void
on_reset(int reason)
{
    ble_synced = false;
}

static void
boot_blink(void)
{
#if MYNEWT_VAL(BREWPILL_BOOT_BLINK)
    int i;

    hal_gpio_init_out(LED_BLINK_PIN, 0);
    for (i = 0; i < 6; i++) {
        hal_gpio_toggle(LED_BLINK_PIN);
        os_time_delay(os_time_ms_to_ticks32(150));
    }
    hal_gpio_write(LED_BLINK_PIN, 0);
#endif
}

static void
meas_task_fn(void *arg)
{
    struct brewpill_reading reading;
    int accel_state;

    memset(&reading, 0, sizeof(reading));
    boot_blink();
    accel_state = lis3dh_init();

    while (1) {
        take_reading(&reading, accel_state);
        if (accel_state < 0) {
            /* Retry in case the sensor was slow to power up. */
            accel_state = lis3dh_init();
        }

        while (!ble_synced) {
            os_time_delay(OS_TICKS_PER_SEC / 10);
        }
        encode_payload(&reading);
        update_advertising();

        os_time_delay(os_time_ms_to_ticks32(
            MYNEWT_VAL(BREWPILL_MEAS_INTERVAL_S) * 1000));
    }
}

int
mynewt_main(int argc, char **argv)
{
    sysinit();

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    os_task_init(&meas_task, "meas", meas_task_fn, NULL, MEAS_TASK_PRIO,
                 OS_WAIT_FOREVER, meas_stack, MEAS_STACK_SIZE);

    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }
    return 0;
}
