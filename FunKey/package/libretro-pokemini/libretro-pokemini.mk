################################################################################
#
# libretro-pokemini
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_POKEMINI_VERSION = 19095803f1434eb980db6e4004187a4b786e20b6
LIBRETRO_POKEMINI_SITE = https://github.com/libretro/pokemini.git
LIBRETRO_POKEMINI_SITE_METHOD = git
LIBRETRO_POKEMINI_LICENSE = GPL-3.0+
LIBRETRO_POKEMINI_LICENSE_FILES = LICENSE

# Same recipe as picoarch's cores target for platform=funkey-s
define LIBRETRO_POKEMINI_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_POKEMINI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pokemini_libretro.so $(TARGET_DIR)/usr/games/pokemini_libretro.so
endef

$(eval $(generic-package))
