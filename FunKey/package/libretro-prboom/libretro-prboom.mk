################################################################################
#
# libretro-prboom
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_PRBOOM_VERSION = 3d32634a343d233b5d44628e802ed8bd2262e424
LIBRETRO_PRBOOM_SITE = https://github.com/mollusk-os/libretro-prboom.git
LIBRETRO_PRBOOM_SITE_METHOD = git
LIBRETRO_PRBOOM_LICENSE = GPL-2.0
LIBRETRO_PRBOOM_LICENSE_FILES = COPYING

# Same recipe as picoarch's cores target for platform=funkey-s, with the
# version string the core reports set as a git checkout of it would set it
define LIBRETRO_PRBOOM_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
		GIT_VERSION='" $(shell echo $(LIBRETRO_PRBOOM_VERSION) | cut -c1-7)"'
endef

define LIBRETRO_PRBOOM_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/prboom_libretro.so $(TARGET_DIR)/usr/local/share/OPKs/Libretro/cores/prboom_libretro.so
endef

$(eval $(generic-package))
