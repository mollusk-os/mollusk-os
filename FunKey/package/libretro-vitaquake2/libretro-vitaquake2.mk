################################################################################
#
# libretro-vitaquake2
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_VITAQUAKE2_VERSION = e664fdb8f39bb7cb72ef09e54caca3826fd69950
LIBRETRO_VITAQUAKE2_SITE = https://github.com/mollusk-os/vitaquake2.git
LIBRETRO_VITAQUAKE2_SITE_METHOD = git
LIBRETRO_VITAQUAKE2_GIT_SUBMODULES = YES
LIBRETRO_VITAQUAKE2_LICENSE = GPL-2.0
LIBRETRO_VITAQUAKE2_LICENSE_FILES = LICENSE

# Same recipe as picoarch's cores target for platform=funkey-s
define LIBRETRO_VITAQUAKE2_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_VITAQUAKE2_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/vitaquake2_libretro.so $(TARGET_DIR)/usr/local/share/OPKs/Libretro/cores/vitaquake2_libretro.so
endef

$(eval $(generic-package))
