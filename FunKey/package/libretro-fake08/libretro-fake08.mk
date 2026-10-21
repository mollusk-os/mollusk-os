################################################################################
#
# libretro-fake08
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_FAKE08_VERSION = 5fc10239c822a4e07a63537d001dbe2c6daf00f6
LIBRETRO_FAKE08_SITE = https://github.com/jtothebell/fake-08.git
LIBRETRO_FAKE08_SITE_METHOD = git
LIBRETRO_FAKE08_GIT_SUBMODULES = YES
LIBRETRO_FAKE08_LICENSE = MIT
LIBRETRO_FAKE08_LICENSE_FILES = LICENSE.MD

# Same recipe as picoarch's cores target for platform=funkey-s
define LIBRETRO_FAKE08_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D)/platform/libretro -f Makefile \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_FAKE08_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/platform/libretro/fake08_libretro.so $(TARGET_DIR)/usr/games/fake08_libretro.so
endef

$(eval $(generic-package))
