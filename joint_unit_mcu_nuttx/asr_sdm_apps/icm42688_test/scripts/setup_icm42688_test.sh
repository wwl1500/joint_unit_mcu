#!/bin/bash

set -e

# Paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJ_ROOT="$(cd "$APP_DIR/../../.." && pwd)"
NUTTX_DIR="$PROJ_ROOT/nuttx"
APPS_DIR="$PROJ_ROOT/apps"

echo "[INFO] Project root: $PROJ_ROOT"
echo "[INFO] NuttX:        $NUTTX_DIR"
echo "[INFO] apps:         $APPS_DIR"
echo "[INFO] app:          $APP_DIR"

if [ ! -d "$NUTTX_DIR" ] || [ ! -d "$APPS_DIR" ]; then
  echo "[ERROR] Missing nuttx/apps under $PROJ_ROOT"
  exit 1
fi

# Link into apps/examples
mkdir -p "$APPS_DIR/examples"
if [ -L "$APPS_DIR/examples/icm42688_test" ] || [ -e "$APPS_DIR/examples/icm42688_test" ]; then
  rm -rf "$APPS_DIR/examples/icm42688_test"
fi
ln -sf "$APP_DIR" "$APPS_DIR/examples/icm42688_test"
echo "[OK] Linked: apps/examples/icm42688_test -> $APP_DIR"

# Re-gen Kconfig indices
pushd "$APPS_DIR" >/dev/null
./tools/mkkconfig.sh
popd >/dev/null

# Configure and enable app
pushd "$NUTTX_DIR" >/dev/null
if [ ! -f .config ]; then
  ./tools/configure.sh xiao-rp2350:usbnsh
fi

if [ -x ./tools/kconfig-tweak ]; then
  ./tools/kconfig-tweak -e CONFIG_EXAMPLES_ICM42688_TEST || true
  # Ensure RP23xx I2C core and I2C1 enabled, plus driver layer
  ./tools/kconfig-tweak -e CONFIG_RP23XX_I2C || true
  ./tools/kconfig-tweak -e CONFIG_RP23XX_I2C1 || true
  ./tools/kconfig-tweak -e CONFIG_RP23XX_I2C_DRIVER || true
else
  grep -q '^CONFIG_EXAMPLES_ICM42688_TEST=y' .config || echo 'CONFIG_EXAMPLES_ICM42688_TEST=y' >> .config
  grep -q '^CONFIG_RP23XX_I2C=y' .config || echo 'CONFIG_RP23XX_I2C=y' >> .config
  grep -q '^CONFIG_RP23XX_I2C1=y' .config || echo 'CONFIG_RP23XX_I2C1=y' >> .config
  grep -q '^CONFIG_RP23XX_I2C_DRIVER=y' .config || echo 'CONFIG_RP23XX_I2C_DRIVER=y' >> .config
fi

# Optional: enable i2ctool if user wants register-level checks (can be toggled later)
if [ "${ENABLE_I2CTOOL:-0}" = "1" ]; then
  if [ -x ./tools/kconfig-tweak ]; then
    ./tools/kconfig-tweak -e CONFIG_SYSTEM_I2CTOOL || true
  else
    grep -q '^CONFIG_SYSTEM_I2CTOOL=y' .config || echo 'CONFIG_SYSTEM_I2CTOOL=y' >> .config
  fi
fi

make olddefconfig
make -j"$(nproc)"
echo "[OK] Build done. Output: $NUTTX_DIR/nuttx.uf2"
popd >/dev/null


