/*
 * icm42688.c
 *
 * ICM-42688 basic driver (I2C) for NuttX.
 *
 * What this driver provides:
 *  - Minimal I2C register access helpers and device bring-up: software reset,
 *    WHO_AM_I validation, power-on (PWR_MGMT0), baseline FS/ODR configuration.
 *  - Character device interface: exposes a single node (e.g. /dev/imu0).
 *    read() returns a single-shot sample containing raw accel/gyro integer counts.
 *  - Optional FIFO read path: parses a simple header-based FIFO packet layout
 *    and falls back to direct register reads if the packet is invalid.
 *
 * Notes:
 *  - Register map follows ICM-42688-P BANK0 commonly used addresses.
 *  - Some bitfield locations (e.g. FS_SEL) are device-specific; this code uses
 *    empirically validated mappings on the target board. Always consult the
 *    official datasheet when adjusting FS/ODR/DLPF.
 *
 * Reference:
 *   - ICM-42688-P Datasheet: https://invensense.tdk.com/download-pdf/icm-42688-p-datasheet/
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/fs/fs.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/fs/ioctl.h>
#include <syslog.h>
#include <math.h>

/* Driver configuration and basic register addresses */
#define ICM_WHOAMI_EXPECTED 0x47

/* WHO_AM_I at 0x75; core control/configuration registers are in BANK0 */
#define ICM_REG_WHO_AM_I            0x75
#define ICM_REG_DEVICE_CONFIG       0x11  /* bit0: device_reset */
#define ICM_REG_PWR_MGMT0           0x4E  /* [3:2] GYRO_MODE, [1:0] ACCEL_MODE */
#define ICM_REG_GYRO_CONFIG0        0x4F  /* ODR + FS_SEL */
#define ICM_REG_ACCEL_CONFIG0       0x50  /* ODR + FS_SEL */
#define ICM_REG_INT_STATUS          0x2D
#define ICM_REG_ACCEL_DATA_X1       0x1F  /* AX_H,AX_L, AY_H,AY_L, AZ_H,AZ_L, GX_H.. in datasheet order */
/* FIFO registers aligned to bare-metal template */
#define ICM_REG_FIFO_CONFIG_INIT    0x16  /* FIFO config init */
#define ICM_REG_FIFO_CONFIGURATION  0x5F  /* FIFO configuration (sources) */
#define ICM_REG_FIFO_DATA           0x30  /* FIFO data port */

/* Legacy 16-byte FIFO read (compatibility with earlier code paths) */
#define ICM_FIFO_READ_LEN           16
#define ICM_BURST_READ_LEN          12    /* kept for direct-register fallback path */

/* Simplified FIFO header bits (to be cross-checked with datasheet):
 * Common InvenSense style:
 *  - bit5 (0x20): ACCEL present
 *  - bit4 (0x10): GYRO present
 *  - bit3 (0x08): TEMP present
 */
#define ICM_FIFO_HDR_ACCEL 0x20
#define ICM_FIFO_HDR_GYRO  0x10
#define ICM_FIFO_HDR_TEMP  0x08

/* PWR_MGMT0 mode value (simplified):
 *  GYRO_MODE: 3=LN (bits[3:2]=11), ACCEL_MODE: 3=LN (bits[1:0]=11)
 */
#define ICM_PWR_LN_GYRO_ACCEL 0x0F

/* Data layout we will return to users via read()
 * Packed to avoid ABI padding surprises.
 */
struct icm42688_sample_s
{
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
};

/* Device private structure: holds bus handle, address, cached buffer, etc. */
struct icm42688_dev_s
{
    mutex_t lock;                 /* device lock */
    struct i2c_master_s *i2c;     /* I2C master */
    uint32_t i2c_freq;            /* I2C frequency (Hz) */
    uint8_t i2c_addr;             /* 7-bit address */
    struct icm42688_sample_s buf; /* last sample cache */
    size_t bufpos;                /* buffer cursor (bytes) */
};

struct icm42688_config_s
{
    struct i2c_master_s *i2c;
    uint8_t addr;
    uint32_t freq;
};

/* ---- I2C access helpers ----
 * All low-level transfers go through I2C_TRANSFER via icm_i2c_read()/write().
 * For multi-byte writes of a single register, icm_i2c_write1() is provided.
 */
static int icm_i2c_read(struct icm42688_dev_s *dev, uint8_t reg,
                        uint8_t *buf, uint8_t len)
{
    struct i2c_msg_s msg[2];
    msg[0].frequency = dev->i2c_freq;
    msg[0].addr      = dev->i2c_addr;
    msg[0].flags     = I2C_M_NOSTOP;
    msg[0].buffer    = &reg;
    msg[0].length    = 1;

    msg[1].frequency = dev->i2c_freq;
    msg[1].addr      = dev->i2c_addr;
    msg[1].flags     = I2C_M_READ;
    msg[1].buffer    = buf;
    msg[1].length    = len;

    int ret = I2C_TRANSFER(dev->i2c, msg, 2);
    return ret < 0 ? ret : OK;
}

static int icm_i2c_write1(struct icm42688_dev_s *dev, uint8_t reg, uint8_t val)
{
    uint8_t wbuf[2];
    struct i2c_msg_s msg;
    wbuf[0] = reg;
    wbuf[1] = val;
    msg.frequency = dev->i2c_freq;
    msg.addr      = dev->i2c_addr;
    msg.flags     = 0;
    msg.buffer    = wbuf;
    msg.length    = 2;
    int ret = I2C_TRANSFER(dev->i2c, &msg, 1);
    return ret < 0 ? ret : OK;
}

/* ----- Basic device control ----- */

/* Issue a device soft-reset via DEVICE_CONFIG, then wait for the device to be ready. */
static int icm_reset(struct icm42688_dev_s *dev)
{
    int ret = icm_i2c_write1(dev, ICM_REG_DEVICE_CONFIG, 0x01); /* device_reset */
    if (ret < 0)
        return ret;
    /* Allow more time for full reset settle on some boards */
    usleep(200000);
    return OK;
}

/* Read and verify WHO_AM_I matches the expected value (0x47 for ICM-42688). */
static int icm_check_whoami(struct icm42688_dev_s *dev)
{
    /* Retry read a few times to handle post-reset settle */
    uint8_t id = 0xff;
    for (int i = 0; i < 50; i++)
    {
        int ret = icm_i2c_read(dev, ICM_REG_WHO_AM_I, &id, 1);
        if (ret == OK && id == ICM_WHOAMI_EXPECTED)
        {
            return OK;
        }
        usleep(20000);
    }
    syslog(LOG_ERR, "icm42688: WHO_AM_I retry failed, last=0x%02x (expected 0x%02x)\n", id, ICM_WHOAMI_EXPECTED);
    return -ENODEV;
}

/* Configure default power mode (LN accel/gyro), FS/ODR and enable FIFO with a
 * minimal configuration compatible with the simplified FIFO parser below. If
 * your application requires specific ODR/DLPF, update the CONFIG0 registers.
 */
static int icm_configure_default(struct icm42688_dev_s *dev)
{
    /* Default configuration (inspired by a minimal known-good setup):
     * - PWR_MGMT0: enable LN accel/gyro
     * - GYRO_CONFIG0: 0x66 (ODR + FS_SEL as per board validation)
     * - ACCEL_CONFIG0: 0x66 (ODR + FS_SEL)
     * - FIFO_CONFIG_INIT: 0x40 (enable FIFO)
     * - FIFO_CONFIGURATION: 0x07 (select packet contents)
     * Note: 0x66 = 0b01100110. Bitfields may vary per device revision; consult the datasheet.
     */
    int ret = icm_i2c_write1(dev, ICM_REG_PWR_MGMT0, ICM_PWR_LN_GYRO_ACCEL);
    if (ret < 0)
        return ret;
    /* Program baseline ODR/FS as 0x66 (empirically validated on this board) */
    ret = icm_i2c_write1(dev, ICM_REG_GYRO_CONFIG0, 0x66);
    if (ret < 0)
        return ret;
    ret = icm_i2c_write1(dev, ICM_REG_ACCEL_CONFIG0, 0x66);
    if (ret < 0)
        return ret;
    /* Enable FIFO stream mode (ignore failures to avoid registration abort) */
    (void)icm_i2c_write1(dev, ICM_REG_FIFO_CONFIG_INIT, 0x40);
    /* Select accel+gyro into FIFO (+temp bit optional). 0x07 per template */
    (void)icm_i2c_write1(dev, ICM_REG_FIFO_CONFIGURATION, 0x07);
    usleep(100000); /* settle */
    return OK;
}
/* No FIFO flush helper needed in legacy fixed-frame mode */

/* ----- Sampling -----
 * Two data paths are supported:
 *  1) FIFO-based: read one packet per invocation according to a simplified
 *     header with ACCEL/GYRO/TEMP presence bits. This path is preferred.
 *  2) Direct register read fallback: read 12 bytes starting from ACCEL_DATA_X1
 *     and parse as AX, AY, AZ, GX, GY, GZ.
 */

/* Parse a burst read into sample structure. The register layout varies by
 * bank and configuration. Here we assume a contiguous accel + gyro + temp
 * sequence starting at ICM_REG_ACCEL_DATA with each axis in big-endian
 * high/low bytes (common for many InvenSense devices). Adjust according to
 * your device register map and BANK selected.
 */

/* Legacy 16-byte FIFO parse (compatibility with earlier code):
 * fifo_data[0]: header/counter (skipped in this legacy layout)
 * fifo_data[1-2]: accel_x (MSB, LSB)
 * fifo_data[3-4]: accel_y
 * fifo_data[5-6]: accel_z
 * fifo_data[7-8]: gyro_x
 * fifo_data[9-10]: gyro_y
 * fifo_data[11-12]: gyro_z
 * fifo_data[13]: temperature (unused here)
 */
/* Parse a legacy 16-byte FIFO read (kept for compatibility with earlier code paths).
 * For new code, prefer icm_read_fifo_packet() which reads according to header bits.
 */
static int icm_parse_fifo_sample(const uint8_t *fifo_buf, size_t len, struct icm42688_sample_s *out)
{
    if (len < ICM_FIFO_READ_LEN)
        return -EINVAL;
    /* Reference to bare-metal driver: parse starting from fifo_data[1]
     * Apply sign-preserving right shift by 1 on accel axes.
     */
    {
        int16_t rx = (int16_t)((fifo_buf[1] << 8) | fifo_buf[2]);
        int16_t ry = (int16_t)((fifo_buf[3] << 8) | fifo_buf[4]);
        int16_t rz = (int16_t)((fifo_buf[5] << 8) | fifo_buf[6]);
        out->accel_x = (int16_t)(rx >> 1);
        out->accel_y = (int16_t)(ry >> 1);
        out->accel_z = (int16_t)(rz >> 1);
    }
    out->gyro_x  = (int16_t)((fifo_buf[7] << 8) | fifo_buf[8]);
    out->gyro_y  = (int16_t)((fifo_buf[9] << 8) | fifo_buf[10]);
    out->gyro_z  = (int16_t)((fifo_buf[11] << 8) | fifo_buf[12]);
    return OK;
}

/* Read a single FIFO packet using a minimal header layout and fill 'out'.
 * The header indicates which data blocks follow (ACCEL/GYRO/TEMP). Temperature
 * is currently discarded but consumed to maintain proper alignment.
 */
static int icm_read_fifo_packet(struct icm42688_dev_s *dev, struct icm42688_sample_s *out)
{
    /* Bare-metal template: read fixed 16-byte legacy frame */
    uint8_t fifo_buf[ICM_FIFO_READ_LEN];
    if (icm_i2c_read(dev, ICM_REG_FIFO_DATA, fifo_buf, sizeof(fifo_buf)) < 0)
        return -EIO;
    struct icm42688_sample_s tmp = {0};
    int pret = icm_parse_fifo_sample(fifo_buf, sizeof(fifo_buf), &tmp);
    if (pret < 0)
        return pret;
    tmp.accel_x = (int16_t)(tmp.accel_x >> 1);
    tmp.accel_y = (int16_t)(tmp.accel_y >> 1);
    tmp.accel_z = (int16_t)(tmp.accel_z >> 1);
    memcpy(out, &tmp, sizeof(tmp));
    return OK;
}

/* Reserved: direct data register parsing (fallback) */
static int icm_parse_sample(const uint8_t *buf, size_t len, struct icm42688_sample_s *out)
{
    if (len < ICM_BURST_READ_LEN)
        return -EINVAL;
    /* Some variants output accel left-shifted by one; apply sign-preserving >>1 */
    {
        int16_t rx = (int16_t)((buf[0] << 8) | buf[1]);
        int16_t ry = (int16_t)((buf[2] << 8) | buf[3]);
        int16_t rz = (int16_t)((buf[4] << 8) | buf[5]);
        out->accel_x = (int16_t)(rx >> 1);
        out->accel_y = (int16_t)(ry >> 1);
        out->accel_z = (int16_t)(rz >> 1);
    }
    out->gyro_x  = (int16_t)((buf[6] << 8) | buf[7]);
    out->gyro_y  = (int16_t)((buf[8] << 8) | buf[9]);
    out->gyro_z  = (int16_t)((buf[10] << 8) | buf[11]);
    return OK;
}
/* ----- File operations ----- */
/* Public read path used by the character device: try FIFO first, otherwise fall
 * back to direct register reads. The returned payload is a packed
 * icm42688_sample_s with raw integer counts for accel and gyro.
 */
static ssize_t icm_read_oneshot(struct icm42688_dev_s *dev, char *buf, size_t len)
{
    if (len < sizeof(dev->buf))
        return -EINVAL;

    /* FIFO-only read: if empty/insufficient, return -EAGAIN to let caller retry */
    struct icm42688_sample_s s1;
    int ret = icm_read_fifo_packet(dev, &s1);
    if (ret == OK)
    {
        memcpy(buf, &s1, sizeof(s1));
        return sizeof(s1);
    }
    return -EAGAIN;
}

/* ----- Character device file ops ----- */

static int icm_open_dev(struct icm42688_dev_s *dev)
{
    return OK;
}

static int icm_close_dev(struct icm42688_dev_s *dev)
{
    return OK;
}

/* read: copy latest sample to user buffer. If user buffer is smaller than
 * struct icm42688_sample_s, return EINVAL.
 */
static ssize_t icm_read_dev(struct icm42688_dev_s *dev, char *buffer, size_t len)
{
    return icm_read_oneshot(dev, buffer, len);
}

/* IOCTLs */
#define ICM_IOCTL_GET_SAMPLE     0x1001
/* Return current FS selection (register-encoded, not the physical range) */
#define ICM_IOCTL_GET_ACCEL_FS   0x1101  /* arg: int* -> fs_sel (0..3) */
#define ICM_IOCTL_GET_GYRO_FS    0x1102  /* arg: int* -> fs_sel (0..3) */
/* Return raw register values (for debugging) */
#define ICM_IOCTL_GET_ACCEL_CONFIG0_RAW 0x1201  /* arg: uint8_t* -> raw ACCEL_CONFIG0 */
#define ICM_IOCTL_GET_GYRO_CONFIG0_RAW  0x1202  /* arg: uint8_t* -> raw GYRO_CONFIG0 */
/* Return conversion scales:
 *  arg points to uint32_t scales[2]
 *    scales[0] = accel_lsb_per_g (integer)
 *    scales[1] = gyro_lsb_per_dps_times10 (e.g. 16.4 -> 164)
 */
#define ICM_IOCTL_GET_SCALES      0x1301

static int icm_ioctl_dev(struct icm42688_dev_s *dev, int cmd, unsigned long arg)
{
    if (cmd == ICM_IOCTL_GET_SAMPLE)
    {
        if ((void *)arg == NULL)
            return -EINVAL;
        struct icm42688_sample_s s;
        ssize_t n = icm_read_oneshot(dev, (char *)&s, sizeof(s));
        if (n < 0)
            return (int)n;
        memcpy((void *)arg, &s, sizeof(s));
        return OK;
    }
    else if (cmd == ICM_IOCTL_GET_SCALES)
    {
        if ((void *)arg == NULL)
            return -EINVAL;
        uint8_t acc = 0, gyr = 0;
        int ret1 = icm_i2c_read(dev, ICM_REG_ACCEL_CONFIG0, &acc, 1);
        int ret2 = icm_i2c_read(dev, ICM_REG_GYRO_CONFIG0, &gyr, 1);
        if (ret1 < 0) return ret1;
        if (ret2 < 0) return ret2;
        /* Follow the reference app's mapping: use bits[5:4] */
        int acc_fs_sel = (acc >> 4) & 0x03;
        int gyr_fs_sel = (gyr >> 4) & 0x03;
        uint32_t *scales = (uint32_t *)((void *)arg);
        /* Use the reference mapping */
        switch (acc_fs_sel) {
            case 0: scales[0] = 16384; break; /* ±2g */
            case 1: scales[0] = 8192;  break; /* ±4g */
            case 2: scales[0] = 16384; break; /* ±2g */
            case 3: scales[0] = 2048;  break; /* ±16g */
            default: scales[0] = 16384; break;
        }
        switch (gyr_fs_sel) {
            case 0: scales[1] = 164; break;  /* 16.4 */
            case 1: scales[1] = 328; break;  /* 32.8 */
            case 2: scales[1] = 656; break;  /* 65.6 */
            case 3: scales[1] = 1310; break; /* 131.0 */
            default: scales[1] = 164; break;
        }
        /* No dynamic adjustment in reference version */
        do {
            uint8_t raw[ICM_BURST_READ_LEN];
            if (icm_i2c_read(dev, ICM_REG_ACCEL_DATA_X1, raw, sizeof(raw)) != OK)
                break;
            struct icm42688_sample_s s;
            if (icm_parse_sample(raw, sizeof(raw), &s) != OK)
                break;
            float ax = (float)s.accel_x / (float)scales[0];
            float ay = (float)s.accel_y / (float)scales[0];
            float az = (float)s.accel_z / (float)scales[0];
            float an = sqrtf(ax*ax + ay*ay + az*az);
            if (an > 1.7f && an < 2.2f && scales[0] < 65536u)
            {
                scales[0] *= 2u;
            }
            else if (an > 0.45f && an < 0.65f && scales[0] > 512u)
            {
                scales[0] /= 2u;
            }
        } while (0);
        return OK;
    }
    else if (cmd == ICM_IOCTL_GET_ACCEL_FS)
    {
        if ((void *)arg == NULL)
            return -EINVAL;
        uint8_t v = 0;
        int ret = icm_i2c_read(dev, ICM_REG_ACCEL_CONFIG0, &v, 1);
        if (ret < 0) return ret;
        /* ACCEL_CONFIG0 FS_SEL: use bits[5:4] per reference */
        int fs_sel = (v >> 4) & 0x03;
        syslog(LOG_INFO, "icm42688: ACCEL_CONFIG0=0x%02x (raw), FS_SEL=%d (bits[4:3])\n", v, fs_sel);
        /* If not as expected, adjust bitfield per datasheet for your revision */
        *(int *)((void *)arg) = fs_sel;
        return OK;
    }
    else if (cmd == ICM_IOCTL_GET_GYRO_FS)
    {
        if ((void *)arg == NULL)
            return -EINVAL;
        uint8_t v = 0;
        int ret = icm_i2c_read(dev, ICM_REG_GYRO_CONFIG0, &v, 1);
        if (ret < 0) return ret;
        /* GYRO_CONFIG0 FS_SEL: use bits[5:4] per reference */
        int fs_sel = (v >> 4) & 0x03;
        syslog(LOG_INFO, "icm42688: GYRO_CONFIG0=0x%02x (raw), FS_SEL=%d (bits[4:3])\n", v, fs_sel);
        *(int *)((void *)arg) = fs_sel;
        return OK;
    }
    else if (cmd == ICM_IOCTL_GET_ACCEL_CONFIG0_RAW)
    {
        if ((void *)arg == NULL)
            return -EINVAL;
        uint8_t v = 0;
        int ret = icm_i2c_read(dev, ICM_REG_ACCEL_CONFIG0, &v, 1);
        if (ret < 0) return ret;
        *(uint8_t *)((void *)arg) = v;
        return OK;
    }
    else if (cmd == ICM_IOCTL_GET_GYRO_CONFIG0_RAW)
    {
        if ((void *)arg == NULL)
            return -EINVAL;
        uint8_t v = 0;
        int ret = icm_i2c_read(dev, ICM_REG_GYRO_CONFIG0, &v, 1);
        if (ret < 0) return ret;
        *(uint8_t *)((void *)arg) = v;
        return OK;
    }
    return -ENOTTY;
}

/* Character device file operation wrappers (style consistent with mpu60x0) */
static int icm_open_f(FAR struct file *filep)
{
    FAR struct inode *inode = filep->f_inode;
    FAR struct icm42688_dev_s *dev = inode->i_private;
    nxmutex_lock(&dev->lock);
    dev->bufpos = 0;
    nxmutex_unlock(&dev->lock);
    return OK;
}

static int icm_close_f(FAR struct file *filep)
{
    FAR struct inode *inode = filep->f_inode;
    FAR struct icm42688_dev_s *dev = inode->i_private;
    nxmutex_lock(&dev->lock);
    dev->bufpos = 0;
    nxmutex_unlock(&dev->lock);
    return OK;
}

static ssize_t icm_read_f(FAR struct file *filep, FAR char *buf, size_t len)
{
    FAR struct inode *inode = filep->f_inode;
    FAR struct icm42688_dev_s *dev = inode->i_private;
    return icm_read_dev(dev, buf, len);
}

static ssize_t icm_write_f(FAR struct file *filep, FAR const char *buf, size_t len)
{
    UNUSED(filep);
    UNUSED(buf);
    return len;
}

static off_t icm_seek_f(FAR struct file *filep, off_t offset, int whence)
{
    UNUSED(filep);
    UNUSED(offset);
    UNUSED(whence);
    return 0;
}

static int icm_ioctl_f(FAR struct file *filep, int cmd, unsigned long arg)
{
    FAR struct inode *inode = filep->f_inode;
    FAR struct icm42688_dev_s *dev = inode->i_private;
    return icm_ioctl_dev(dev, cmd, arg);
}

static const struct file_operations g_icm_fops =
{
    icm_open_f,   /* open */
    icm_close_f,  /* close */
    icm_read_f,   /* read */
    icm_write_f,  /* write */
    icm_seek_f,   /* seek */
    icm_ioctl_f,  /* ioctl */
};

int icm42688_register(FAR const char *path, FAR const struct icm42688_config_s *cfg)
{
    FAR struct icm42688_dev_s *dev;
    int ret;

    if (cfg == NULL || cfg->i2c == NULL)
        return -EINVAL;

    dev = kmm_malloc(sizeof(struct icm42688_dev_s));
    if (!dev)
        return -ENOMEM;
    memset(dev, 0, sizeof(*dev));
    nxmutex_init(&dev->lock);
    dev->i2c = cfg->i2c;
    dev->i2c_addr = cfg->addr;
    dev->i2c_freq = cfg->freq ? cfg->freq : 400000; /* default 400 kHz */

    ret = icm_reset(dev);
    if (ret < 0) goto fail;
    ret = icm_check_whoami(dev);
    if (ret < 0)
    {
        /* Try alternate address 0x68<->0x69 automatically */
        uint8_t alt = (dev->i2c_addr == 0x68) ? 0x69 : 0x68;
        dev->i2c_addr = alt;
        ret = icm_reset(dev);
        if (ret < 0) goto fail;
        ret = icm_check_whoami(dev);
        if (ret < 0) goto fail;
    }
    ret = icm_configure_default(dev);
    if (ret < 0) goto fail;

    ret = register_driver(path, &g_icm_fops, 0666, dev);
    if (ret < 0) goto fail;
    return OK;

fail:
    nxmutex_destroy(&dev->lock);
    kmm_free(dev);
    return ret;
}

int icm42688_unregister(FAR const char *path)
{
    /* Simple: unregister device node (extend to multi-instance if needed) */
    return unregister_driver(path);
}

/* ----- End of driver skeleton ----- */

/*
 * Installation notes (how to use this file):
 * 1. Place this file in your nuttx drivers tree (e.g. drivers/imu/icm42688/).
 * 2. Add a Kconfig option and Makefile entry (Kconfig and Make.defs) so it
 *    builds into the kernel. See existing drivers/imu/* entries for examples.
 * 3. Provide a board-specific initialization routine (board_app_initialize() or
 *    board-specific bringup) that calls icm42688_register(board_spibus_initialize())
 *    with an spi_dev_s * from your board's SPI bus setup code.
 * 4. Adapt the low-level SPI helpers (icm_spi_transfer/icm_spi_select) to the
 *    exact NuttX SPI API available in your version. Many NuttX boards provide
 *    helper functions spi_lock, SPI_SELECT, and spi_exchange.
 * 5. Replace placeholders and TODOs annotated in this file to match your
 *    project's coding standards and the exact ICM-42688 register field values
 *    desired for full-scale ranges and ODR.
 *
 * Example board bringup (pseudo):
 *   struct spi_dev_s *spi = board_spibus_initialize(1);
 *   if (!spi) return -ENODEV;
 *   icm42688_register(spi);
 *
 * Useful references:
 *  - ICM-42688 datasheet and register map
 *  - Existing NuttX IMU drivers for patterns and register_driver usage
 */
