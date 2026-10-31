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

#include <string.h>
#include "os/mynewt.h"
#include "hal/hal_i2c.h"
#include "lis3dh.h"

#define LIS3DH_I2C_NUM          0
#define LIS3DH_ADDR_ALT         (MYNEWT_VAL(LIS3DH_I2C_ADDR) ^ 0x01)
#define LIS3DH_TIMEOUT          (OS_TICKS_PER_SEC / 10)

#define REG_WHO_AM_I            0x0f
#define REG_CTRL_REG1           0x20
#define REG_CTRL_REG4           0x23
#define REG_STATUS_REG          0x27
#define REG_OUT_X_L             0x28
#define REG_AUTO_INCREMENT      0x80

#define WHO_AM_I_VALUE          0x33

/* ODR = 50 Hz, normal mode, X/Y/Z enabled. */
#define CTRL_REG1_50HZ_XYZ      0x47
/* Power-down mode: ~0.5 uA. */
#define CTRL_REG1_POWER_DOWN    0x00
/* Block data update, +/-2 g, high-resolution (12 bit, 1 mg/digit). */
#define CTRL_REG4_BDU_HR_2G     0x88
#define STATUS_ZYXDA            0x08

static uint8_t lis3dh_addr = MYNEWT_VAL(LIS3DH_I2C_ADDR);

static int
lis3dh_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    struct hal_i2c_master_data data = {
        .address = lis3dh_addr,
        .len = sizeof(buf),
        .buffer = buf,
    };

    return hal_i2c_master_write(LIS3DH_I2C_NUM, &data, LIS3DH_TIMEOUT, 1);
}

static int
lis3dh_read_regs(uint8_t reg, uint8_t *out, uint16_t len)
{
    struct hal_i2c_master_data data = {
        .address = lis3dh_addr,
        .len = 1,
        .buffer = &reg,
    };
    int rc;

    if (len > 1) {
        reg |= REG_AUTO_INCREMENT;
    }
    rc = hal_i2c_master_write(LIS3DH_I2C_NUM, &data, LIS3DH_TIMEOUT, 0);
    if (rc != 0) {
        return rc;
    }
    data.len = len;
    data.buffer = out;
    return hal_i2c_master_read(LIS3DH_I2C_NUM, &data, LIS3DH_TIMEOUT, 1);
}

static int
lis3dh_check_id(uint8_t addr)
{
    uint8_t id = 0;

    lis3dh_addr = addr;
    if (lis3dh_read_regs(REG_WHO_AM_I, &id, 1) != 0) {
        return -1;
    }
    return id == WHO_AM_I_VALUE ? 0 : -1;
}

int
lis3dh_init(void)
{
    int found;

    if (lis3dh_check_id(MYNEWT_VAL(LIS3DH_I2C_ADDR)) == 0) {
        found = 0;
    } else if (lis3dh_check_id(LIS3DH_ADDR_ALT) == 0) {
        found = 1;
    } else {
        lis3dh_addr = MYNEWT_VAL(LIS3DH_I2C_ADDR);
        return -1;
    }

    if (lis3dh_write_reg(REG_CTRL_REG4, CTRL_REG4_BDU_HR_2G) != 0 ||
        lis3dh_write_reg(REG_CTRL_REG1, CTRL_REG1_POWER_DOWN) != 0) {
        return -1;
    }
    return found;
}

static int
lis3dh_wait_ready(void)
{
    uint8_t status;
    int tries;

    /* At 50 Hz a new sample is ready every 20 ms. */
    for (tries = 0; tries < 10; tries++) {
        if (lis3dh_read_regs(REG_STATUS_REG, &status, 1) != 0) {
            return -1;
        }
        if (status & STATUS_ZYXDA) {
            return 0;
        }
        os_time_delay(os_time_ms_to_ticks32(5));
    }
    return -1;
}

int
lis3dh_measure(int count, struct lis3dh_sample *avg)
{
    uint8_t raw[6];
    int32_t sx = 0, sy = 0, sz = 0;
    int i;
    int rc;

    memset(avg, 0, sizeof(*avg));
    if (count <= 0) {
        return -1;
    }

    rc = lis3dh_write_reg(REG_CTRL_REG4, CTRL_REG4_BDU_HR_2G);
    rc |= lis3dh_write_reg(REG_CTRL_REG1, CTRL_REG1_50HZ_XYZ);
    if (rc != 0) {
        return rc;
    }

    /* High-resolution turn-on time is 7/ODR = 140 ms; discard that data. */
    os_time_delay(os_time_ms_to_ticks32(160));

    for (i = 0; i < count; i++) {
        rc = lis3dh_wait_ready();
        if (rc == 0) {
            rc = lis3dh_read_regs(REG_OUT_X_L, raw, sizeof(raw));
        }
        if (rc != 0) {
            break;
        }
        /* 12-bit left-justified, 1 mg/digit at +/-2 g in HR mode. */
        sx += (int16_t)(raw[0] | (raw[1] << 8)) >> 4;
        sy += (int16_t)(raw[2] | (raw[3] << 8)) >> 4;
        sz += (int16_t)(raw[4] | (raw[5] << 8)) >> 4;
    }

    lis3dh_write_reg(REG_CTRL_REG1, CTRL_REG1_POWER_DOWN);
    if (rc != 0) {
        return rc;
    }

    avg->x_mg = sx / count;
    avg->y_mg = sy / count;
    avg->z_mg = sz / count;
    return 0;
}
