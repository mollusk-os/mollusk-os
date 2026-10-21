################################################################################
#
# libretro-tyrquake
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_TYRQUAKE_VERSION = f1749e7f7941373ba92373b2d7ec2451c88204bc
LIBRETRO_TYRQUAKE_SITE = https://github.com/mollusk-os/tyrquake.git
LIBRETRO_TYRQUAKE_SITE_METHOD = git
LIBRETRO_TYRQUAKE_LICENSE = GPL-2.0
LIBRETRO_TYRQUAKE_LICENSE_FILES = LICENSE.txt

# Same recipe as picoarch's cores target for platform=funkey-s, with the
# version string the core reports set as a git checkout of it would set it
define LIBRETRO_TYRQUAKE_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
		GIT_VERSION='" $(shell echo $(LIBRETRO_TYRQUAKE_VERSION) | cut -c1-7)"'
endef

define LIBRETRO_TYRQUAKE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tyrquake_libretro.so $(TARGET_DIR)/usr/local/share/OPKs/Libretro/cores/tyrquake_libretro.so
endef

$(eval $(generic-package))
