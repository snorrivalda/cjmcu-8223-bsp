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

/* Minimal polled driver for the LIS3DH accelerometer on the CJMCU-8223. */

#ifndef H_BREWPILL_LIS3DH_
#define H_BREWPILL_LIS3DH_

#include <stdint.h>

struct lis3dh_sample {
    int32_t x_mg;
    int32_t y_mg;
    int32_t z_mg;
};

/**
 * Probes for the LIS3DH at the configured address, then at the alternate
 * address (SA0 low).  Leaves the device in power-down mode.
 *
 * @return 0 if found at LIS3DH_I2C_ADDR, 1 if found at the alternate
 *         address, negative if no LIS3DH answered.
 */
int lis3dh_init(void);

/**
 * Powers the sensor up, averages @p count readings (high-resolution mode,
 * +/-2 g, 50 Hz) and powers it back down.
 *
 * @return 0 on success, non-zero on I2C error.
 */
int lis3dh_measure(int count, struct lis3dh_sample *avg);

#endif
