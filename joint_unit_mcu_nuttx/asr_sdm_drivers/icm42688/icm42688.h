#ifndef __ASR_SDM_DRIVERS_ICM42688_H
#define __ASR_SDM_DRIVERS_ICM42688_H

#include <nuttx/config.h>
#include <stdint.h>
#include <sys/types.h>

/* Forward declaration to avoid heavy include here */
struct i2c_master_s; /* from <nuttx/i2c/i2c_master.h> */

/* Public sample data layout, matching icm42688.c */
struct icm42688_sample_s
{
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
};

/* Register-time configuration provided by board/app code */
struct icm42688_config_s
{
    struct i2c_master_s *i2c;
    uint8_t  addr;   /* 7-bit I2C address */
    uint32_t freq;   /* I2C frequency in Hz; 0 -> default 400k */
};

/* IOCTL command definitions (kept in sync with icm42688.c) */
#define ICM_IOCTL_GET_SAMPLE              0x1001
#define ICM_IOCTL_GET_ACCEL_FS            0x1101
#define ICM_IOCTL_GET_GYRO_FS             0x1102
#define ICM_IOCTL_GET_ACCEL_CONFIG0_RAW   0x1201
#define ICM_IOCTL_GET_GYRO_CONFIG0_RAW    0x1202
#define ICM_IOCTL_GET_SCALES              0x1301

#ifdef __cplusplus
extern "C" {
#endif

int icm42688_register(FAR const char *path,
                      FAR const struct icm42688_config_s *cfg);
int icm42688_unregister(FAR const char *path);

#ifdef __cplusplus
}
#endif

#endif /* __ASR_SDM_DRIVERS_ICM42688_H */


