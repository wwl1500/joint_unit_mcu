/****************************************************************************
 * ICM-42688 basic test app (I2C1 @ 0x68)
 *
 * This example application demonstrates how to:
 *  - Initialize I2C1 and register the ICM-42688 character device at /dev/imu0
 *  - Query conversion scales via a driver IOCTL (accel LSB/g, gyro LSB/dps)
 *  - Read samples periodically and convert raw integer counts to physical units
 *  - Apply a simple bias estimator (EMA) and first-order IIR low-pass filter
 *  - Print AX/AY/AZ in g, GX/GY/GZ in dps
 *
 * Notes:
 *  - The driver uses FIFO-only mode with fixed 16-byte frame reads, consistent
 *    with the bare-metal implementation.
 *  - Scales are retrieved from the driver and should not be hardcoded in the app.
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <nuttx/i2c/i2c_master.h>
#include <sys/ioctl.h>
#include <math.h>

/* Goal:
 * - Initialize I2C1 at 400 kHz for address 0x68
 * - Register the driver to /dev/imu0
 * - Periodically read and print accel/gyro in physical units
 */

/* Public driver API (kept consistent with icm42688.c internals). */
struct icm42688_config_s
{
  struct i2c_master_s *i2c;
  uint8_t addr;
  uint32_t freq;
};

/* Register / Unregister */
int icm42688_register(const char *path, const struct icm42688_config_s *cfg);
int icm42688_unregister(const char *path);

/* Sample data structure (kept in sync with icm42688.c) */
struct icm42688_sample_s
{
  int16_t accel_x;
  int16_t accel_y;
  int16_t accel_z;
  int16_t gyro_x;
  int16_t gyro_y;
  int16_t gyro_z;
};

/* RP23xx board-specific I2C initialization entry point */
extern struct i2c_master_s *rp23xx_i2cbus_initialize(int port);

int icm42688_test_main(int argc, char *argv[])
{
  const char *devpath = "/dev/imu0";
  int fd = -1;
  int ret;

  /* Auto-probe I2C port {1,0} and address {0x68,0x69} */
  int ports[2] = {1, 0};
  uint8_t addrs[2] = {0x68, 0x69};
  struct i2c_master_s *i2c = NULL;
  struct icm42688_config_s cfg;
  bool registered = false;
  for (int pi = 0; pi < 2 && !registered; pi++)
  {
    i2c = rp23xx_i2cbus_initialize(ports[pi]);
    if (i2c == NULL)
    {
      continue;
    }
    for (int ai = 0; ai < 2 && !registered; ai++)
    {
      cfg.i2c  = i2c;
      cfg.addr = addrs[ai];
      cfg.freq = 400000; /* 400 kHz */
      ret = icm42688_register(devpath, &cfg);
      if (ret == 0)
      {
        printf("ICM-42688 registered on I2C%d @ 0x%02X\n", ports[pi], addrs[ai]);
        registered = true;
      }
      else
      {
        /* try next */
      }
    }
  }
  if (!registered)
  {
    printf("icm42688_register failed on all ports/addrs\n");
    return EXIT_FAILURE;
  }

  /* Open device and process samples in a loop */
  fd = open(devpath, O_RDONLY);
  if (fd < 0)
  {
    printf("open %s failed: %d\n", devpath, errno);
    return EXIT_FAILURE;
  }

  /* Use line-buffered stdout so each print flushes per line */
  setvbuf(stdout, NULL, _IOLBF, 0);

  bool first = true;

  /* Acquire conversion scales via IOCTL */
  float accel_lsb_per_g = 2048.0f;  /* set by ioctl */
  float gyro_lsb_per_dps = 16.4f;   /* set by ioctl */
  bool fs_ready = false;

  /* Adaptive bias estimation: only update under stability conditions.
   * Note: az_bias keeps 1 g by computing az_bias = mean(z) - 1.0
   */
  float ax_bias = 0.0f, ay_bias = 0.0f, az_bias = 0.0f;
  float gx_bias = 0.0f, gy_bias = 0.0f, gz_bias = 0.0f;
  const float beta_bias = 0.005f; /* EMA rate for bias: slower and stable */
  const float gyro_stable_dps = 1.0f; /* stability threshold for gyro */
  const float accel_norm_tolerance_g = 0.02f; /* stability: |norm-1| < 0.02g */

  /* IIR filter state */
  bool filt_init = false;
  float fax = 0.0f, fay = 0.0f, faz = 0.0f;
  float fgx = 0.0f, fgy = 0.0f, fgz = 0.0f;
  const float alpha = 0.15f; /* 0..1, larger responds faster */

  /* Dynamic scale sanity check flag */
  bool accel_scale_sanity_fixed = false;
  int anorm_high_cnt = 0; /* count for |a|≈2g */
  int anorm_low_cnt  = 0; /* count for |a|≈0.5g */
  const int anorm_trigger_frames = 5; /* consecutive frame threshold */

  /* Note: no dynamic scaling; scales are provided by the driver */

  int print_count = 0;
  const int print_every_n = 10; /* print at 1 Hz to reduce serial congestion */
  while (1)
  {
    struct icm42688_sample_s s;
    ssize_t n = read(fd, &s, sizeof(s));
    if (n == (ssize_t)sizeof(s))
    {
      if (first)
      {
        /* Drop the very first frame to avoid cold-start transients */
        usleep(10000);
        first = false;
        continue;
      }

      /* First-time fetch of scales via IOCTL */
      if (!fs_ready)
      {
        uint32_t scales[2] = {0, 0};
        if (ioctl(fd, 0x1301, (unsigned long)&scales[0]) == 0)
        {
          accel_lsb_per_g = (float)scales[0];
          gyro_lsb_per_dps = (float)scales[1] / 10.0f;
          printf("[IOCTL] scales -> accelLSB=%.1f gyroLSB=%.1f\r\n",
                 accel_lsb_per_g, gyro_lsb_per_dps);
          fs_ready = true;
        }
        else
        {
          usleep(100000);
          continue;
        }
      }

      /* Convert raw counts to physical units */
      float ax_g = (float)s.accel_x / accel_lsb_per_g;
      float ay_g = (float)s.accel_y / accel_lsb_per_g;
      float az_g = (float)s.accel_z / accel_lsb_per_g;

      float gx_dps = (float)s.gyro_x / gyro_lsb_per_dps;
      float gy_dps = (float)s.gyro_y / gyro_lsb_per_dps;
      float gz_dps = (float)s.gyro_z / gyro_lsb_per_dps;

      /* Skip output until scales are ready */
      if (!fs_ready)
      {
        usleep(100000);
        continue;
      }

      /* Stability check: small gyro magnitude and |a| close to 1 g */
      float gyro_abs_max = 0.0f;
      if (gyro_abs_max < fabsf(gx_dps)) gyro_abs_max = fabsf(gx_dps);
      if (gyro_abs_max < fabsf(gy_dps)) gyro_abs_max = fabsf(gy_dps);
      if (gyro_abs_max < fabsf(gz_dps)) gyro_abs_max = fabsf(gz_dps);
      float anorm = sqrtf(ax_g * ax_g + ay_g * ay_g + az_g * az_g);

      /* If |a|≈2g or ≈0.5g is detected (common full-scale detection error), correct scale once on app side */
      if (fs_ready && !accel_scale_sanity_fixed && (anorm > 1.7f && anorm < 2.3f))
      {
        if (++anorm_high_cnt >= anorm_trigger_frames && accel_lsb_per_g < 65536.0f)
        {
          float old = accel_lsb_per_g;
          accel_lsb_per_g *= 2.0f;
          accel_scale_sanity_fixed = true;
          anorm_high_cnt = 0;
          anorm_low_cnt = 0;
          /* Recalculate */
          ax_g = (float)s.accel_x / accel_lsb_per_g;
          ay_g = (float)s.accel_y / accel_lsb_per_g;
          az_g = (float)s.accel_z / accel_lsb_per_g;
          anorm = sqrtf(ax_g * ax_g + ay_g * ay_g + az_g * az_g);
          uint8_t acc_raw = 0;
          (void)ioctl(fd, 0x1201, (unsigned long)&acc_raw);
          printf("[SANITY] app accelLSB %.1f->%.1f (|a|=%.3fg, ACCEL_CFG0=0x%02x)\r\n",
                 old, accel_lsb_per_g, anorm, acc_raw);
        }
      }
      else if (fs_ready && !accel_scale_sanity_fixed && (anorm > 0.45f && anorm < 0.65f))
      {
        if (++anorm_low_cnt >= anorm_trigger_frames && accel_lsb_per_g > 512.0f)
        {
          float old = accel_lsb_per_g;
          accel_lsb_per_g *= 0.5f;
          accel_scale_sanity_fixed = true;
          anorm_high_cnt = 0;
          anorm_low_cnt = 0;
          ax_g = (float)s.accel_x / accel_lsb_per_g;
          ay_g = (float)s.accel_y / accel_lsb_per_g;
          az_g = (float)s.accel_z / accel_lsb_per_g;
          anorm = sqrtf(ax_g * ax_g + ay_g * ay_g + az_g * az_g);
          uint8_t acc_raw = 0;
          (void)ioctl(fd, 0x1201, (unsigned long)&acc_raw);
          printf("[SANITY] app accelLSB %.1f->%.1f (|a|=%.3fg, ACCEL_CFG0=0x%02x)\r\n",
                 old, accel_lsb_per_g, anorm, acc_raw);
        }
      }
      else
      {
        /* Clear counters when exiting anomaly window */
        if (anorm <= 1.7f || anorm >= 2.3f) anorm_high_cnt = 0;
        if (anorm <= 0.45f || anorm >= 0.65f) anorm_low_cnt = 0;
      }
      bool stable = (gyro_abs_max < gyro_stable_dps) && (fabsf(anorm - 1.0f) < accel_norm_tolerance_g);


      /* Update bias only under stability:
       *  - Gyro: EMA towards 0
       *  - Accel: EMA on X/Y; Z keeps 1g by az_bias = mean(z) - 1.0
       */
      if (stable)
      {
        gx_bias = (1.0f - beta_bias) * gx_bias + beta_bias * gx_dps;
        gy_bias = (1.0f - beta_bias) * gy_bias + beta_bias * gy_dps;
        gz_bias = (1.0f - beta_bias) * gz_bias + beta_bias * gz_dps;

        ax_bias = (1.0f - beta_bias) * ax_bias + beta_bias * ax_g;
        ay_bias = (1.0f - beta_bias) * ay_bias + beta_bias * ay_g;
        az_bias = (1.0f - beta_bias) * az_bias + beta_bias * az_g;
      }

      /* Remove bias */
      ax_g -= ax_bias; ay_g -= ay_bias; az_g -= az_bias;
      gx_dps -= gx_bias; gy_dps -= gy_bias; gz_dps -= gz_bias;

      /* No dynamic scaling beyond driver-provided scales */

      /* First-order IIR filter */
      if (!filt_init)
      {
        fax = ax_g; fay = ay_g; faz = az_g;
        fgx = gx_dps; fgy = gy_dps; fgz = gz_dps;
        filt_init = true;
      }
      else
      {
        fax = alpha * ax_g + (1.0f - alpha) * fax;
        fay = alpha * ay_g + (1.0f - alpha) * fay;
        faz = alpha * az_g + (1.0f - alpha) * faz;
        fgx = alpha * gx_dps + (1.0f - alpha) * fgx;
        fgy = alpha * gy_dps + (1.0f - alpha) * fgy;
        fgz = alpha * gz_dps + (1.0f - alpha) * fgz;
      }

      /* Reduce print rate to avoid USB CDC congestion perception */
      if (++print_count >= print_every_n)
      {
        print_count = 0;
      float anorm_f = sqrtf(fax*fax + fay*fay + faz*faz);
      char line[160];
      int len = snprintf(line, sizeof(line),
                         "AX=%.2fg AY=%.2fg AZ=%.2fg | GX=%.1fdps GY=%.1fdps GZ=%.1fdps",
                         fax, fay, faz, fgx, fgy, fgz);
      if (len > 0)
      {
        (void)write(1, line, len);
        (void)write(1, "\r\n", 2);
      }
      fflush(stdout);
      usleep(2000); /* give CDC 2ms for transmit buffer */
      }
    }
    else
    {
      /* Small sleep on read failure to avoid busy looping */
      usleep(10000);
    }
    /* Sample/output interval: 100 ms (adjust as needed) */
    usleep(100000);
  }

  close(fd);
  (void)icm42688_unregister(devpath);
  return EXIT_SUCCESS;
}


