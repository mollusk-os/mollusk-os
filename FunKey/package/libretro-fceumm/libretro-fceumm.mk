################################################################################
#
# libretro-fceumm
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_FCEUMM_VERSION = 5cd4a43e16a7f3cd35628d481c347a0a98cfdfa2
LIBRETRO_FCEUMM_SITE = https://github.com/libretro/libretro-fceumm.git
LIBRETRO_FCEUMM_SITE_METHOD = git
LIBRETRO_FCEUMM_LICENSE = GPL-2.0
LIBRETRO_FCEUMM_LICENSE_FILES = Copying

# Same recipe as picoarch's cores target for platform=funkey-s, with the
# version string the core reports set as a git checkout of it would set it
define LIBRETRO_FCEUMM_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) -f Makefile.libretro \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
		GIT_VERSION='" $(shell echo $(LIBRETRO_FCEUMM_VERSION) | cut -c1-7)"'
endef

define LIBRETRO_FCEUMM_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/fceumm_libretro.so $(TARGET_DIR)/usr/games/fceumm_libretro.so
endef

$(eval $(generic-package))
