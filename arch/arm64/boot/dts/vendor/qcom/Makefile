ifeq ($(CONFIG_MACH_XIAOMI),y)
ifeq ($(CONFIG_MACH_XIAOMI_SM8350),y)
ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)

ifeq ($(CONFIG_MACH_XIAOMI_CETUS),y)
dtbo-$(CONFIG_MACH_XIAOMI_CETUS) +=\
	cetus-sm8350-overlay.dtbo
cetus-sm8350-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
endif

ifeq ($(CONFIG_MACH_XIAOMI_HAYDN),y)
dtbo-$(CONFIG_MACH_XIAOMI_HAYDN +=\
	haydn-sm8350-overlay.dtbo
haydn-sm8350-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
endif

ifeq ($(CONFIG_MACH_XIAOMI_MARS),y)
dtbo-$(CONFIG_MACH_XIAOMI_MARS) +=\
	mars-sm8350-overlay.dtbo \
	star-sm8350-overlay.dtbo
mars-sm8350-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
star-sm8350-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
endif

ifeq ($(CONFIG_MACH_XIAOMI_STAR),y)
dtbo-$(CONFIG_MACH_XIAOMI_STAR) +=\
	mars-sm8350-overlay.dtbo \
	star-sm8350-overlay.dtbo
mars-sm8350-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
star-sm8350-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
endif

ifeq ($(CONFIG_MACH_XIAOMI_ODIN),y)
dtbo-$(CONFIG_MACH_XIAOMI_ODIN) +=\
	odin-sm8350-overlay.dtbo
odin-sm8350-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
endif

ifeq ($(CONFIG_MACH_XIAOMI_VENUS),y)
dtbo-$(CONFIG_MACH_XIAOMI_VENUS) +=\
	venus-sm8350-overlay.dtbo
venus-sm8350-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
endif

ifeq ($(CONFIG_MACH_XIAOMI_VILI),y)
dtbo-$(CONFIG_MACH_XIAOMI_VILI) +=\
	vili-sm8350-overlay.dtbo
vili-sm8350-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
endif

endif
endif

ifeq ($(CONFIG_MACH_XIAOMI_SM7350),y)
ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)

ifeq ($(CONFIG_MACH_XIAOMI_RENOIR),y)
dtbo-$(CONFIG_MACH_XIAOMI_RENOIR) +=\
	renoir-sm7350-overlay.dtbo
renoir-sm7350-overlay.dtbo-base := shima.dtb
endif

endif
endif

ifeq ($(CONFIG_MACH_XIAOMI_SM7325),y)
ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)

ifeq ($(CONFIG_MACH_XIAOMI_LISA),y)
dtbo-$(CONFIG_MACH_XIAOMI_LISA) +=\
	lisa-sm7325-overlay.dtbo
lisa-sm7325-overlay.dtbo-base := yupik.dtb
endif

ifeq ($(CONFIG_MACH_XIAOMI_MONA),y)
dtbo-$(CONFIG_MACH_XIAOMI_MONA) +=\
	mona-sm7325-overlay.dtbo
mona-sm7325-overlay.dtbo-base := yupik.dtb
endif

ifeq ($(CONFIG_MACH_XIAOMI_TAOYAO),y)
dtbo-$(CONFIG_MACH_XIAOMI_TAOYAO) +=\
	taoyao-sm7325-overlay.dtbo
taoyao-sm7325-overlay.dtbo-base := yupik.dtb
endif

ifeq ($(CONFIG_MACH_XIAOMI_ZIJIN),y)
dtbo-$(CONFIG_MACH_XIAOMI_ZIJIN) +=\
	zijin-sm7325-overlay.dtbo
zijin-sm7325-overlay.dtbo-base := yupik.dtb
endif

ifeq ($(CONFIG_MACH_XIAOMI_REDWOOD),y)
dtbo-$(CONFIG_MACH_XIAOMI_REDWOOD) +=\
	zijin-sm7325-overlay.dtbo
zijin-sm7325-overlay.dtbo-base := yupik.dtb
endif

endif
endif

else

ifneq ($(CONFIG_ARCH_QTI_VM), y)
ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_ARCH_LAHAINA) += lahaina-rumi-overlay.dtbo \
	lahaina-mtp-overlay.dtbo \
	cetus-sm8350-overlay.dtbo \
	star-sm8350-overlay.dtbo \
	odin-sm8350-overlay.dtbo \
	venus-sm8350-overlay.dtbo \
	haydn-sm8350-overlay.dtbo \
	mars-sm8350-overlay.dtbo \
	vili-sm8350-overlay.dtbo \
	lahaina-mtp-hsp-overlay.dtbo \
	lahaina-cdp-overlay.dtbo \
	lahaina-qrd-overlay.dtbo \
	lahaina-atp-overlay.dtbo \
	lahaina-hdk-overlay.dtbo \
	lahaina-hhg-overlay.dtbo \
	lahainap-hhg-hsp-pro-overlay.dtbo \
	lahaina-qrd-hsp-overlay.dtbo \
	lahaina-qrd-module-overlay.dtbo \
	lahainap-mtp-overlay.dtbo \
	lahainap-cdp-overlay.dtbo \
	lahainap-atp-overlay.dtbo \
	lahainap-qrd-overlay.dtbo \
	lahaina-cdp-v2.2-overlay.dtbo \
	lahaina-mtp-v2-overlay.dtbo \
	lahaina-mtp-v2.1-overlay.dtbo \
	lahaina-mtp-hsp-pro-overlay.dtbo \
	lahaina-cdp-hsp-pro-overlay.dtbo \
	lahaina-qrd-hsp-pro-overlay.dtbo

lahaina-rumi-overlay.dtbo-base := lahaina.dtb
lahaina-mtp-hsp-overlay.dtbo-base := lahaina.dtb
cetus-sm8350-overlay.dtbo-base := lahaina.dtb
star-sm8350-overlay.dtbo-base := lahaina.dtb
odin-sm8350-overlay.dtbo-base := lahaina.dtb
venus-sm8350-overlay.dtbo-base := lahaina.dtb
haydn-sm8350-overlay.dtbo-base := lahaina.dtb
mars-sm8350-overlay.dtbo-base := lahaina.dtb
vili-sm8350-overlay.dtbo-base := lahaina.dtb
lahaina-mtp-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
lahaina-cdp-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
lahaina-atp-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
lahaina-hdk-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
lahaina-hhg-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
lahainap-hhg-hsp-pro-overlay.dtbo-base := lahainap.dtb lahainap-v2.dtb lahainap-v2.1.dtb
lahaina-qrd-hsp-overlay.dtbo-base := lahaina.dtb
lahaina-qrd-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
lahaina-qrd-module-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
lahainap-mtp-overlay.dtbo-base := lahainap.dtb lahainap-v2.dtb lahainap-v2.1.dtb
lahainap-cdp-overlay.dtbo-base := lahainap.dtb lahainap-v2.dtb lahainap-v2.1.dtb
lahainap-atp-overlay.dtbo-base := lahainap.dtb lahainap-v2.dtb lahainap-v2.1.dtb
lahainap-qrd-overlay.dtbo-base := lahainap.dtb lahainap-v2.dtb lahainap-v2.1.dtb
lahaina-cdp-v2.2-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb \
					lahaina-v2.1.dtb
lahaina-mtp-v2-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb \
					lahaina-v2.1.dtb
lahaina-mtp-v2.1-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb \
					lahaina-v2.1.dtb
lahaina-mtp-hsp-pro-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb \
					lahaina-v2.1.dtb
lahaina-cdp-hsp-pro-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb \
					lahaina-v2.1.dtb
lahaina-qrd-hsp-pro-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb \
					lahaina-v2.1.dtb
else
dtb-$(CONFIG_ARCH_LAHAINA) += lahaina-rumi.dtb \
	lahaina-mtp-hsp.dtb \
	lahaina-mtp.dtb \
	lahaina-cdp.dtb \
	lahaina-atp.dtb \
	lahaina-hdk.dtb \
	lahaina-qrd-hsp.dtb \
	lahaina-qrd.dtb \
	lahaina-qrd-module.dtb \
	lahainap-mtp.dtb \
	lahainap-cdp.dtb \
	lahainap-atp.dtb \
	lahainap-qrd.dtb \
	lahaina-v2-mtp.dtb \
	lahaina-v2-cdp.dtb \
	lahaina-v2-atp.dtb \
	lahaina-v2-hdk.dtb \
	lahaina-v2-qrd.dtb \
	lahaina-v2-qrd-module.dtb \
	lahainap-v2-mtp.dtb \
	lahainap-v2-cdp.dtb \
	lahainap-v2-atp.dtb \
	lahainap-v2-qrd.dtb \
	lahaina-cdp-v2.2.dtb \
	lahaina-v2.1-mtp.dtb \
	lahaina-v2.1-cdp.dtb \
	lahaina-v2.1-atp.dtb \
	lahaina-v2.1-hdk.dtb \
	lahaina-v2.1-hhg.dtb \
	lahainap-v2.1-hhg-hsp-pro.dtb \
	lahaina-v2.1-qrd.dtb \
	lahaina-v2.1-qrd-module.dtb \
	lahainap-v2.1-mtp.dtb \
	lahainap-v2.1-cdp.dtb \
	lahainap-v2.1-atp.dtb \
	lahainap-v2.1-qrd.dtb \
	lahaina-mtp-v2.dtb \
	lahaina-mtp-v2.1.dtb \
	lahaina-v2.1-mtp-hsp-pro.dts \
	lahaina-v2.1-cdp-hsp-pro.dts \
	lahaina-v2.1-qrd-hsp-pro.dts
endif

ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_ARCH_HOLI) += holi-rumi-overlay.dtbo \
	holi-mtp-overlay.dtbo \
	holi-cdp-overlay.dtbo \
	holi-cdp-lcd-overlay.dtbo \
	holi-qrd-overlay.dtbo \
	holi-atp-overlay.dtbo \
	holi-mtp-usbc-overlay.dtbo \
	holi-mtp-pm6125-overlay.dtbo \
	holi-cdp-pm6125-overlay.dtbo \
	holi-cdp-lcd-pm6125-overlay.dtbo \
	holi-mtp-usbc-pm6125-overlay.dtbo \
	holi-mtp-pm6125-nopmi-overlay.dtbo \
	holi-qrd-pm6125-overlay.dtbo \
	holi-qrd-pm6125-nopmi-overlay.dtbo

holi-rumi-overlay.dtbo-base := holi.dtb
holi-mtp-overlay.dtbo-base := holi.dtb
holi-cdp-overlay.dtbo-base := holi.dtb
holi-cdp-lcd-overlay.dtbo-base := holi.dtb
holi-qrd-overlay.dtbo-base := holi.dtb
holi-atp-overlay.dtbo-base := holi.dtb
holi-mtp-usbc-overlay.dtbo-base := holi.dtb
holi-mtp-pm6125-overlay.dtbo-base := holi.dtb
holi-cdp-pm6125-overlay.dtbo-base := holi.dtb
holi-cdp-lcd-pm6125-overlay.dtbo-base := holi.dtb
holi-mtp-usbc-pm6125-overlay.dtbo-base := holi.dtb
holi-mtp-pm6125-nopmi-overlay.dtbo-base := holi.dtb
holi-qrd-pm6125-overlay.dtbo-base := holi.dtb
holi-qrd-pm6125-nopmi-overlay.dtbo-base := holi.dtb

else
dtb-$(CONFIG_ARCH_HOLI) += holi-rumi.dtb \
	holi-mtp.dtb \
	holi-cdp.dtb \
	holi-cdp-lcd.dtb \
	holi-qrd.dtb \
	holi-atp.dtb \
	holi-mtp-usbc.dtb\
	holi-mtp-pm6125.dtb \
	holi-cdp-pm6125.dtb \
	holi-cdp-lcd-pm6125.dtb \
	holi-mtp-usbc-pm6125.dtb \
	holi-mtp-pm6125-nopmi.dtb \
	holi-qrd-pm6125.dtb \
	holi-qrd-pm6125-nopmi.dtb

endif

ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_ARCH_BLAIR) += blair-rumi-overlay.dtbo \
	blair-mtp-overlay.dtbo \
	blair-cdp-overlay.dtbo \
	blair-atp-overlay.dtbo \
	blair-qrd-overlay.dtbo \
	blair-mtp-usbc-overlay.dtbo \
	blair-mtp-nopmi-overlay.dtbo \
	blair-qrd-nopmi-overlay.dtbo \
	blairp-mtp-overlay.dtbo \
	blairp-cdp-overlay.dtbo \
	blairp-atp-overlay.dtbo \
	blairp-qrd-overlay.dtbo \
	blairp-mtp-usbc-overlay.dtbo \
	blairp-mtp-nopmi-overlay.dtbo \
	blairp-qrd-nopmi-overlay.dtbo

blair-rumi-overlay.dtbo-base := blair.dtb
blair-mtp-overlay.dtbo-base := blair.dtb
blair-cdp-overlay.dtbo-base := blair.dtb
blair-atp-overlay.dtbo-base := blair.dtb
blair-qrd-overlay.dtbo-base := blair.dtb
blair-mtp-usbc-overlay.dtbo-base := blair.dtb
blair-mtp-nopmi-overlay.dtbo-base := blair.dtb
blair-qrd-nopmi-overlay.dtbo-base := blair.dtb
blairp-mtp-overlay.dtbo-base := blairp.dtb
blairp-cdp-overlay.dtbo-base := blairp.dtb
blairp-atp-overlay.dtbo-base := blairp.dtb
blairp-qrd-overlay.dtbo-base := blairp.dtb
blairp-mtp-usbc-overlay.dtbo-base := blairp.dtb
blairp-mtp-nopmi-overlay.dtbo-base := blairp.dtb
blairp-qrd-nopmi-overlay.dtbo-base := blairp.dtb
else
dtb-$(CONFIG_ARCH_BLAIR) += blair-rumi.dtb \
	blair-mtp.dtb \
	blair-cdp.dtb \
	blair-atp.dtb \
	blair-qrd.dtb \
	blair-mtp-usbc.dtb \
	blair-mtp-nopmi.dtb \
	blair-qrd-nopmi.dtb \
	blairp-mtp.dtb \
	blairp-cdp.dtb \
	blairp-atp.dtb \
	blairp-qrd.dtb \
	blairp-mtp-usbc.dtb \
	blairp-mtp-nopmi.dtb \
	blairp-qrd-nopmi.dtb
endif

ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_ARCH_SHIMA) +=    \
	renoir-sm7350-overlay.dtbo	\
	shima-rumi-overlay.dtbo \
	shima-idp-overlay.dtbo  \
	shima-idp-120fps-overlay.dtbo \
	shima-atp-overlay.dtbo \
	shima-qrd-overlay.dtbo \
	shima-idps-digital-mics-overlay.dtbo \
	shima-idps-digital-mics-120fps-overlay.dtbo

renoir-sm7350-overlay.dtbo-base := shima.dtb
shima-rumi-overlay.dtbo-base := shima.dtb
shima-idp-overlay.dtbo-base := shima.dtb
shima-idp-120fps-overlay.dtbo-base := shima.dtb
shima-atp-overlay.dtbo-base := shima.dtb
shima-qrd-overlay.dtbo-base := shima.dtb
shima-idps-digital-mics-overlay.dtbo-base := shima.dtb
shima-idps-digital-mics-120fps-overlay.dtbo-base := shima.dtb
else
dtb-$(CONFIG_ARCH_SHIMA) += shima-rumi.dtb  \
			    shima-idp.dtb   \
			    shima-idp-120fps.dtb  \
			    shima-atp.dtb \
			    shima-qrd.dtb \
			    shima-idps-digital-mics.dtb \
			    shima-idps-digital-mics-120fps.dtb
endif

ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_ARCH_YUPIK) +=    \
	lisa-sm7325-overlay.dtbo \
	mona-sm7325-overlay.dtbo \
	taoyao-sm7325-overlay.dtbo \
	zijin-sm7325-overlay.dtbo \
	redwood-sm7325-overlay.dtbo \
	yupik-rumi-overlay.dtbo \
	yupik-idp-overlay.dtbo \
	yupik-atp-overlay.dtbo \
	yupik-qrd-overlay.dtbo \
	yupik-idps-amoled-overlay.dtbo \
	yupik-idp-pm7250b-overlay.dtbo \
	yupik-idp-hsp-overlay.dtbo \
	yupikp-idp-overlay.dtbo \
	yupikp-atp-overlay.dtbo \
	yupikp-qrd-overlay.dtbo \
	yupikp-idps-amoled-overlay.dtbo \
	yupikp-idp-hsp-overlay.dtbo \
	yupik-iot-idp-overlay.dtbo \
	yupik-iot-idp-pm7250b-overlay.dtbo \
	yupik-iot-qrd-overlay.dtbo \
	yupik-iot-hsp-overlay.dtbo \
	yupik-iot-hsp-pm7250b-overlay.dtbo \
	katmai-hsp-overlay.dtbo \
	katmai-hsp-pm7250b-overlay.dtbo \
	katmai-fp2-hsp-overlay.dtbo \
	katmai-fp2-hsp-pm7250b-overlay.dtbo \
	yupik-iot-idps-amoled-overlay.dtbo \
	yupikp-iot-idp-overlay.dtbo \
	yupikp-iot-idp-pm7250b-overlay.dtbo \
	yupikp-iot-qrd-overlay.dtbo \
	yupikp-iot-hsp-overlay.dtbo \
	yupikp-iot-hsp-pm7250b-overlay.dtbo \
	katmaip-hsp-overlay.dtbo \
	katmaip-hsp-pm7250b-overlay.dtbo \
	katmaip-fp2-hsp-overlay.dtbo \
	katmaip-fp2-hsp-pm7250b-overlay.dtbo \
	yupikp-iot-idps-amoled-overlay.dtbo

lisa-sm7325-overlay.dtbo-base := yupik.dtb
mona-sm7325-overlay.dtbo-base := yupik.dtb
taoyao-sm7325-overlay.dtbo-base := yupik.dtb
zijin-sm7325-overlay.dtbo-base := yupik.dtb
redwood-sm7325-overlay.dtbo-base := yupik.dtb
yupik-rumi-overlay.dtbo-base := yupik.dtb
yupik-idp-overlay.dtbo-base := yupik.dtb
yupik-atp-overlay.dtbo-base := yupik.dtb
yupik-qrd-overlay.dtbo-base := yupik.dtb
yupik-idps-amoled-overlay.dtbo-base := yupik.dtb
yupik-idp-pm7250b-overlay.dtbo-base := yupik.dtb
yupik-idp-hsp-overlay.dtbo-base := yupik-hsp.dtb
yupikp-idp-overlay.dtbo-base := yupikp.dtb
yupikp-atp-overlay.dtbo-base := yupikp.dtb
yupikp-qrd-overlay.dtbo-base := yupikp.dtb
yupikp-idps-amoled-overlay.dtbo-base := yupikp.dtb
yupikp-idp-hsp-overlay.dtbo-base := yupikp-hsp.dtb
yupik-iot-idp-overlay.dtbo-base := yupik-iot.dtb katmai.dtb katmai-fp2.dtb
yupik-iot-idp-pm7250b-overlay.dtbo-base := yupik-iot.dtb katmai.dtb katmai-fp2.dtb
yupik-iot-qrd-overlay.dtbo-base := yupik-iot.dtb katmai.dtb katmai-fp2.dtb
yupik-iot-hsp-overlay.dtbo-base := yupik-iot.dtb
yupik-iot-hsp-pm7250b-overlay.dtbo-base := yupik-iot.dtb
katmai-hsp-overlay.dtbo-base :=  katmai.dtb
katmai-hsp-pm7250b-overlay.dtbo-base :=  katmai.dtb
katmai-fp2-hsp-overlay.dtbo-base :=  katmai-fp2.dtb
katmai-fp2-hsp-pm7250b-overlay.dtbo-base :=  katmai-fp2.dtb
yupik-iot-idps-amoled-overlay.dtbo-base := yupik-iot.dtb katmai.dtb katmai-fp2.dtb
yupikp-iot-idp-overlay.dtbo-base := yupikp-iot.dtb katmaip.dtb katmaip-fp2.dtb
yupikp-iot-idp-pm7250b-overlay.dtbo-base := yupikp-iot.dtb katmaip.dtb katmaip-fp2.dtb
yupikp-iot-qrd-overlay.dtbo-base := yupikp-iot.dtb katmaip.dtb katmaip-fp2.dtb
yupikp-iot-hsp-overlay.dtbo-base := yupikp-iot.dtb
yupikp-iot-hsp-pm7250b-overlay.dtbo-base := yupikp-iot.dtb
katmaip-hsp-overlay.dtbo-base := katmaip.dtb
katmaip-hsp-pm7250b-overlay.dtbo-base := katmaip.dtb
katmaip-fp2-hsp-overlay.dtbo-base := katmaip-fp2.dtb
katmaip-fp2-hsp-pm7250b-overlay.dtbo-base := katmaip-fp2.dtb
yupikp-iot-idps-amoled-overlay.dtbo-base := yupikp-iot.dtb katmaip.dtb katmaip-fp2.dtb
else
dtb-$(CONFIG_ARCH_YUPIK) += yupik-rumi.dtb \
	yupik-idp.dtb \
	yupik-atp.dtb \
	yupik-qrd.dtb \
	yupik-idps-amoled.dtb \
	yupik-idp-pm7250b.dtb \
	yupik-idp-hsp.dtb \
	yupikp-idp.dtb \
	yupikp-atp.dtb \
	yupikp-qrd.dtb \
	yupikp-idps-amoled.dtb \
	yupikp-idp-hsp.dtb \
	yupik-iot-idp.dtb \
	yupik-iot-idp-pm7250b.dtb \
	yupik-iot-qrd.dtb \
	yupik-iot-hsp.dtb \
	yupik-iot-idps-amoled.dtb \
	yupikp-iot-idp.dtb \
	yupikp-iot-idp-pm7250b.dtb \
	yupikp-iot-qrd.dtb \
	yupikp-iot-hsp.dtb \
	yupik-iot-hsp-pm7250b.dtb \
	yupikp-iot-hsp-pm7250b.dtb \
	yupikp-iot-idps-amoled.dtb \
	katmai-idp.dtb \
	katmai-idp-pm7250b.dtb \
	katmai-qrd.dtb \
	katmai-hsp.dtb \
	katmai-idps-amoled.dtb \
	katmaip-idp.dtb \
	katmaip-idp-pm7250b.dtb \
	katmaip-qrd.dtb \
	katmaip-hsp.dtb \
	katmai-hsp-pm7250b.dtb \
	katmaip-hsp-pm7250b.dtb \
	katmaip-idps-amoled.dtb
endif

ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_ARCH_MONACO) +=    \
	monaco-rumi-overlay.dtbo \
	monaco-standalone-idp-v1-overlay.dtbo \
	monaco-standalone-idp-v2-overlay.dtbo \
	monaco-standalone-idp-v3-overlay.dtbo \
	monaco-idp-v1-overlay.dtbo \
	monaco-idp-v2-overlay.dtbo \
	monaco-idp-v3-overlay.dtbo

monaco-rumi-overlay.dtbo-base := monaco.dtb
monaco-standalone-idp-v1-overlay.dtbo-base := monaco.dtb
monaco-standalone-idp-v2-overlay.dtbo-base := monaco.dtb
monaco-standalone-idp-v3-overlay.dtbo-base := monaco.dtb
monaco-idp-v1-overlay.dtbo-base := monaco.dtb monacop.dtb
monaco-idp-v2-overlay.dtbo-base := monaco.dtb
monaco-idp-v3-overlay.dtbo-base := monaco.dtb

else
dtb-$(CONFIG_ARCH_MONACO) += monaco-rumi.dtb \
	monaco-standalone-idp-v1.dtb \
	monaco-standalone-idp-v2.dtb \
	monaco-standalone-idp-v3.dtb \
	monaco-idp-v1.dtb \
	monaco-idp-v2.dtb \
	monaco-idp-v3.dtb \
	monacop-idp-v1.dtb
endif

dtb-$(CONFIG_ARCH_SCUBA) += qrb2210-rb1.dtb

ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
ifeq ($(CONFIG_AUTO_LXC_OVERLAY), y)
	dtbo-$(CONFIG_ARCH_SA8155) += \
		sa8155-adp-star-lxc-overlay.dtbo \
		sa8155p-adp-star-lxc-overlay.dtbo \
		sa8155-v2-adp-air-lxc-overlay.dtbo \
		sa8155p-v2-adp-air-lxc-overlay.dtbo
else
	dtbo-$(CONFIG_ARCH_SA8155) += \
		sa8155-adp-star-overlay.dtbo \
		sa8155p-adp-star-overlay.dtbo \
		sa8155-v2-adp-air-overlay.dtbo \
		sa8155p-v2-adp-air-overlay.dtbo
endif

sa8155-adp-star-overlay.dtbo-base := sa8155.dtb sa8155-v2.dtb
sa8155p-adp-star-overlay.dtbo-base := sa8155p.dtb sa8155p-v2.dtb
sa8155-v2-adp-air-overlay.dtbo-base := sa8155.dtb sa8155-v2.dtb
sa8155p-v2-adp-air-overlay.dtbo-base := sa8155p.dtb sa8155p-v2.dtb
sa8155-adp-star-lxc-overlay.dtbo-base := sa8155.dtb sa8155-v2.dtb
sa8155p-adp-star-lxc-overlay.dtbo-base := sa8155p.dtb sa8155p-v2.dtb
sa8155-v2-adp-air-lxc-overlay.dtbo-base := sa8155.dtb sa8155-v2.dtb
sa8155p-v2-adp-air-lxc-overlay.dtbo-base := sa8155p.dtb sa8155p-v2.dtb
else
dtb-$(CONFIG_ARCH_SA8155)       += sa8155-adp-star.dtb \
	sa8155p-adp-star.dtb \
	sa8155-v2-adp-star.dtb \
	sa8155p-v2-adp-star.dtb \
	sa8155-v2-adp-air.dtb \
	sa8155p-v2-adp-air.dtb
endif

ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
ifeq ($(CONFIG_AUTO_LXC_OVERLAY), y)
	dtbo-$(CONFIG_ARCH_SA6155) += \
		sa6155p-adp-star-lxc-overlay.dtbo \
		sa6155p-v2-adp-star-lxc-overlay.dtbo \
		sa6155p-adp-air-lxc-overlay.dtbo \
		sa6155p-v2-adp-air-lxc-overlay.dtbo
else
	dtbo-$(CONFIG_ARCH_SA6155) += \
		sa6155-adp-star-overlay.dtbo \
		sa6155p-adp-star-overlay.dtbo \
		sa6155p-adp-star-lpass-overlay.dtbo \
		sa6155-adp-air-overlay.dtbo \
		sa6155p-adp-air-overlay.dtbo \
		sa6155p-v2-adp-star-overlay.dtbo \
		sa6155p-v2-adp-air-overlay.dtbo \
		sa6155p-v2-adp-air-lpass-overlay.dtbo
endif

sa6155-adp-star-overlay.dtbo-base := sa6155.dtb
sa6155p-adp-star-overlay.dtbo-base := sa6155p.dtb
sa6155p-adp-star-lpass-overlay.dtbo-base :=sa6155p.dtb
sa6155p-v2-adp-star-overlay.dtbo-base := sa6155p.dtb
sa6155-adp-air-overlay.dtbo-base := sa6155.dtb
sa6155p-adp-air-overlay.dtbo-base := sa6155p.dtb
sa6155p-v2-adp-air-overlay.dtbo-base := sa6155p.dtb
sa6155p-v2-adp-air-lpass-overlay.dtbo-base := sa6155p.dtb
sa6155p-adp-star-lxc-overlay.dtbo-base := sa6155p.dtb
sa6155p-v2-adp-star-lxc-overlay.dtbo-base := sa6155p.dtb
sa6155p-adp-air-lxc-overlay.dtbo-base := sa6155p.dtb
sa6155p-v2-adp-air-lxc-overlay.dtbo-base := sa6155p.dtb
else
dtb-$(CONFIG_ARCH_SA6155)       += sa6155-adp-star.dtb \
	sa6155p-adp-star.dtb \
	sa6155p-adp-star-lpass.dtb \
	sa6155-adp-air.dtb \
	sa6155p-adp-air.dtb \
	sa6155p-v2-adp-star.dtb \
	sa6155p-v2-adp-air.dtb \
	sa6155p-v2-adp-air-lpass.dtb
endif

ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
ifeq ($(CONFIG_AUTO_LXC_OVERLAY), y)
	dtbo-$(CONFIG_ARCH_SA8195) += \
		sa8195p-adp-star-lxc-overlay.dtbo \
		sa8195p-v2-adp-air-lxc-overlay.dtbo
else
	dtbo-$(CONFIG_ARCH_SA8195) += \
		sa8195p-adp-star-overlay.dtbo \
		sa8195p-v2-adp-air-overlay.dtbo
endif

sa8195p-adp-star-overlay.dtbo-base := sa8195p.dtb
sa8195p-v2-adp-air-overlay.dtbo-base := sa8195p.dtb
sa8195p-adp-star-lxc-overlay.dtbo-base := sa8195p.dtb
sa8195p-v2-adp-air-lxc-overlay.dtbo-base := sa8195p.dtb
else
dtb-$(CONFIG_ARCH_SA8195)	+= sa8195p-adp-star.dtb \
	sa8195p-v2-adp-air.dtb
endif

dtb-$(CONFIG_ARCH_SA2150P) += sa2150p-ccard-emmc.dtb
endif

ifeq ($(CONFIG_ARCH_LAHAINA), y)
ifeq ($(CONFIG_ARCH_QTI_VM), y)
dtb-$(CONFIG_ARCH_QTI_VM) += lahaina-vm-mtp.dtb \
	lahaina-vm-cdp.dtb \
	lahaina-vm-qrd.dtb \
	lahaina-vm-mtp-v2.dtb \
	lahaina-vm-mtp-v2.1.dtb
endif
endif

dtb-$(CONFIG_ARCH_SDXLEMUR) += sdxlemur-rumi.dtb \
	sdxlemur-atp.dtb \
	sdxlemur-cdp.dtb \
	sdxlemur-mtp.dtb \
	sdxlemur-mtp-mbb.dtb \
	sdxlemur-mtp-cpe.dtb \
	sdxlemur-mtp-cpe-pine1x.dtb \
	sdxlemur-mtp-cpe-pine2x.dtb \
	sdxlemur-mtp-mbb-emmc.dtb \
	sdxlemur-mtp-mbb-emmc-512mb.dtb \
	sdxlemur-mtp-mbb-ntn3-pcie.dtb \
	sdxlemur-mtp-mbb-pcie-ep.dtb \
	sdxlemur-mtp-mbb-pcie-ep-ipq.dtb \
	sdxlemur-mtp-mbb-pcie-rc-ep.dtb \
	sdxlemur-mtp-mbb-m2-ep.dtb \
	sdxlemur-mtp-mbb-m2-ep-usb.dtb \
	sdxlemur-mtp-mbb.dtb \
	sdxlemur-rcm-pcie-ep.dtb \
	sdxlemur-rcm-rc.dtb \
	sdxlemur-rcm-rc-pine1x.dtb \
	sdxlemur-rcm-rc-pine2x.dtb \
	sdxlemur-v2-cdp.dtb \
	sdxlemur-v2-mtp-mbb.dtb \
	sdxlemur-v2-mtp-cpe.dtb \
	sdxlemur-v2-mtp-mbb-emmc.dtb \
	sdxlemur-v2-mtp-mbb-ntn3-pcie.dtb \
	sdxlemur-v2-mtp-mbb-pcie-ep.dtb \
	sdxlemur-v2-mtp-mbb-pcie-ep-ipq.dtb \
	sdxlemur-v2-mtp-mbb-pcie-rc-ep.dtb \
	sdxlemur-v2-rcm-pcie-ep.dtb \
	sdxlemur-v2-rcm-rc.dtb \
	sdxlemur-v2-atp.dtb \
	sdxlemur-v2-mtp-cpe-pine1x.dtb \
	sdxlemur-v2-mtp-cpe-pine2x.dtb \
	sdxlemur-v2-mtp-mbb-emmc-512mb.dtb \
	sdxlemur-v2-mtp-mbb-m2-ep-usb.dtb \
	sdxlemur-v2-mtp-mbb-m2-ep.dtb \
	sdxlemur-v2-rcm-rc-pine1x.dtb \
	sdxlemur-v2-rcm-rc-pine2x.dtb \
	sdxlemur-v2-m2-ep-fwa.dtb \
	sdxlemur-v2-mtp-cpe-pine2x-5+6.dtb



ifeq ($(CONFIG_ARCH_SHIMA), y)
ifeq ($(CONFIG_ARCH_QTI_VM), y)
dtb-$(CONFIG_ARCH_QTI_VM) += shima-vm-rumi.dtb \
	shima-vm-atp.dtb \
	shima-vm-idp.dtb \
	shima-vm-qrd.dtb
endif
endif

dtb-$(CONFIG_ARCH_SDXNIGHTJAR) += sdxnightjar-mtp.dtb \
	sdxnightjar-cdp.dtb \
	sdxnightjar-qrd.dtb \
	sdxnightjar-dualwifi-mtp.dtb \
	sdxnightjar-512mb-mtp.dtb \
	sdxnightjar-dualwifi-512mb-mtp.dtb

ifeq ($(CONFIG_ARCH_YUPIK), y)
ifeq ($(CONFIG_ARCH_QTI_VM), y)
dtb-$(CONFIG_ARCH_QTI_VM) += yupik-vm-rumi.dtb \
	yupik-vm-atp.dtb \
	yupik-vm-idp.dtb \
	yupik-vm-qrd.dtb
endif
endif

dtb-$(CONFIG_QTI_QUIN_GVM) += sa8155-vm-la.dtb \
	sa8155-vm-lv.dtb \
	sa8155-vm-la-mt.dtb \
	sa8155-vm-lv-mt.dtb \
	sa8195-vm-la.dtb \
	sa8195-vm-lv.dtb \
	sa8195-vm-la-mt.dtb \
	sa8195-vm-lv-mt.dtb \
	sa8195-vm-lv-lxc.dtb \
	sa6155p-vm-la.dtb \
	direwolf-vm-lv.dtb \
	direwolf-vm-la.dtb

ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
	dtbo-$(CONFIG_ARCH_DIREWOLF) += direwolf-ivi-rumi-overlay.dtbo \
		direwolf-ivi-adp-star-overlay.dtbo \
		direwolf-adas-rumi-overlay.dtbo \
		direwolf-adas-adp-star-overlay.dtbo

direwolf-ivi-rumi-overlay.dtbo-base := direwolf-ivi.dtb
direwolf-ivi-adp-star-overlay.dtbo-base := direwolf-ivi.dtb
direwolf-adas-rumi-overlay.dtbo-base := direwolf-adas.dtb
direwolf-adas-adp-star-overlay.dtbo-base := direwolf-adas.dtb
else
dtb-$(CONFIG_ARCH_DIREWOLF) += direwolf-ivi-rumi.dtb \
	direwolf-ivi-adp-star.dtb \
	direwolf-adas-rumi.dtb \
	direwolf-adas-adp-star.dtb
endif

endif
always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb *.dtbo
