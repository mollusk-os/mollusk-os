#############################################################
#
# gmenu2x
#
#############################################################

GMENU2X_VERSION = 8dbc7bfc482262ec0d23c1abad6089aef4bcb6d0
GMENU2X_SITE_METHOD = git
GMENU2X_SITE = https://github.com/mollusk-os/gmenu2x.git
GMENU2X_LICENSE = GPL-2.0

GMENU2X_DEPENDENCIES = sdl sdl_ttf sdl_gfx dejavu libpng fonts-droid

GMENU2X_CONF_OPTS = -DBIND_CONSOLE=ON

ifeq ($(BR2_PACKAGE_GMENU2X_SHOW_CLOCK),y)
GMENU2X_CONF_OPTS += -DCLOCK=ON
else
GMENU2X_CONF_OPTS += -DCLOCK=OFF
endif

ifeq ($(BR2_PACKAGE_GMENU2X_CPUFREQ),y)
GMENU2X_CONF_OPTS += -DCPUFREQ=ON
else
GMENU2X_CONF_OPTS += -DCPUFREQ=OFF
endif

ifeq ($(BR2_PACKAGE_LIBOPK),y)
GMENU2X_DEPENDENCIES += libopk
endif

ifeq ($(BR2_PACKAGE_LIBXDGMIME),y)
GMENU2X_DEPENDENCIES += libxdgmime
endif

$(eval $(cmake-package))
