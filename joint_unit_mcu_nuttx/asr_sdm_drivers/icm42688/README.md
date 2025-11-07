# 在 NuttX 下编写与集成自定义传感器驱动（以 ICM-42688 为例）


## 1. 目录与文件放置
- 驱动源码（本例）：`asr_sdm_drivers/icm42688/icm42688.c`
- 驱动的 Kconfig/Makefile：可放同目录（用于被上层工程或应用直接纳入），或按 NuttX 官方推荐放入 `nuttx/drivers/*` 并配套 Kconfig/Make.defs 集成。
- 测试应用建议：放在 `asr_sdm_apps/<your_app>/`，通过符号链接接入 `apps/examples/<your_app>`。

## 2. 最小驱动结构要点
- 头文件：
  - `<nuttx/i2c/i2c_master.h>` I2C 主机接口
  - `<nuttx/fs/fs.h>` 字符设备注册 `register_driver`
  - `<nuttx/kmalloc.h>` 动态内存
  - `<nuttx/mutex.h>` 互斥
  - `<syslog.h>` 日志
- 设备结构 `dev_s`：保存 `i2c_master_s*`、地址、频率、互斥、缓存等。
- 低层 I2C 访问：使用 `I2C_TRANSFER()` 组合写-读消息完成寄存器读写与突发读取。
- 初始化流程（以 ICM-42688 为例）：
  1) 软复位（`DEVICE_CONFIG`）并等待稳定
  2) 读 `WHO_AM_I` 校验芯片 ID（ICM-42688-P 期望 0x47）
  3) 上电/工作模式（`PWR_MGMT0`）
  4) 量程/ODR/LPF 配置（`GYRO_CONFIG0`、`ACCEL_CONFIG0`）
  5) FIFO：启用（`FIFO_CONFIG_INIT`/`FIFO_CONFIGURATION`）。驱动使用 FIFO-only 模式，固定16字节帧读取，与裸跑程序实现保持一致
- 字符设备节点：实现 `open/close/read/ioctl` 并用 `register_driver(path, &fops, 0666, dev)` 注册，例如 `/dev/imu0`。

参考数据手册：ICM-42688-P Datasheet（寄存器与时序）
- https://invensense.tdk.com/download-pdf/icm-42688-p-datasheet/

## 3. I2C 总线初始化（板级）
- 由板级提供的 I2C 初始化函数获取 `i2c_master_s*`，RP23xx 上为：
  - `rp23xx_i2cbus_initialize(int port)`（需启用 `CONFIG_RP23XX_I2C`, `CONFIG_RP23XX_I2C1` 等）
- 将 `i2c` 指针、7 位地址与频率组合为配置传入驱动注册函数：
```c
struct icm42688_config_s cfg = {
  .i2c  = rp23xx_i2cbus_initialize(1),
  .addr = 0x68,
  .freq = 400000,
};
icm42688_register("/dev/imu0", &cfg);
```

## 4. 测试应用与外部应用接入
- 在 `apps/` 之外放置应用（外部应用），例如 `asr_sdm_apps/icm42688_test/`，其 `Makefile` 可直接引入驱动源码：
```make
CSRCS += ../../asr_sdm_drivers/icm42688/icm42688.c
```
- 通过符号链接接入：
```bash
cd joint_unit_mcu_nuttx/apps
mkdir -p examples
ln -sf ../../asr_sdm_apps/icm42688_test examples/icm42688_test
./tools/mkkconfig.sh
```
- 启用应用与板卡配置并编译固件：
```bash
cd ../nuttx
./tools/configure.sh xiao-rp2350:usbnsh
# 启用：
echo "CONFIG_EXAMPLES_ICM42688_TEST=y" >> .config
# RP23xx I2C 支持：
echo "CONFIG_RP23XX_I2C=y" >> .config
echo "CONFIG_RP23XX_I2C1=y" >> .config
echo "CONFIG_RP23XX_I2C_DRIVER=y" >> .config
make olddefconfig
make -j$(nproc)
```

## 5. 运行与验证
- 烧录 `nuttx.uf2` 后进入 NSH：
  - 运行 `icm42688_test`，打印加速度/陀螺原始值或换算值（g/dps）
  - 可选启用 `i2ctool` 后：`i2c dev 1; i2c read 0x68 0x75 1` 期望读回 `0x47`（参阅 NuttX I2C 工具用法）
- NuttX I2C 工具参考：
  - https://www.embeddedrelated.com/showarticle/1656.php

## 6. 常见扩展
- ioctl 支持：
  - 设置量程（±2/4/8/16g；±250/500/1000/2000 dps）
  - 设置 ODR/LPF，读取当前采样率
  - 获取换算系数：`ICM_IOCTL_GET_SCALES` 返回 `accel_lsb_per_g` 与 `gyro_lsb_per_dps*10`，便于上层直接换算
- 数据路径：
  - 当前实现：FIFO-only 模式，固定16字节帧读取（与裸跑程序实现保持一致）
- 数据换算：
  - 按当前量程的 LSB/单位系数转换为 g/dps；随 ioctl 自动更新系数
- 标定与滤波：
  - 静止零偏估计与扣除；一阶低通滤波（IIR）


## 7. 参考工程与文档
- ICM-42688-P Datasheet：
  - https://invensense.tdk.com/download-pdf/icm-42688-p-datasheet/
- NuttX I2C 设备与工具教程：
  - https://www.embeddedrelated.com/showarticle/1656.php
  - https://www.embeddedrelated.com/showarticle/1668.php
- XIAO RP2350 与 NuttX（构建/烧录/NSH）：
  - https://wiki.seeedstudio.com/cn/xiao_rp2350_nuttx/

---

