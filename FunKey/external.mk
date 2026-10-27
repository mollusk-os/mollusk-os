include $(sort $(wildcard $(BR2_EXTERNAL_FUNKEY_PATH)/package/*/*.mk))

# The Mollusk OS boot logo (branding/boot/) in place of the FunKey one in the kernel sources
define FUNKEY_MOLLUSK_BOOT_LOGO
	cp $(BR2_EXTERNAL_FUNKEY_PATH)/../branding/boot/logo_funkey_clut224.ppm \
		$(@D)/drivers/video/logo/logo_funkey_clut224.ppm
endef
LINUX_POST_PATCH_HOOKS += FUNKEY_MOLLUSK_BOOT_LOGO
