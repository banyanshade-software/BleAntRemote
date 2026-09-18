#!/bin/sh

MDIR="$(cd "$(dirname "$0")" && pwd)"

NRFUTIL=/opt/nordic/ncs/toolchains/185bb0e3b6/nrfutil/bin/nrfutil
#NRFUTIL=/opt/nordic/ncs/toolchains/ccc010f809/nrfutil/bin/nrfutil



#$NRFUTIL device list

#$NRFUTIL install nrf5sdk-tools
#$NRFUTIL install device
#exit 0

#H=build/gopro_remote_fw/zephyr/zephyr.hex
H=build/merged.hex
#H=build_2/gopro_remote_fw/zephyr/zephyr.hex
#H=build_2/gopro_remote_fw/zephyr/zephyr.hex

rm -f app_dfu_package.zip

if true; then
	$NRFUTIL pkg generate --hw-version 52 --sd-req 0x00 \
	  --application $H --application-version 1 \
	  app_dfu_package.zip || exit 1
	#$NRFUTIL pkg generate \ # --hw-version 52 --sd-req 0x00 \
	#  --application $H --application-version 1 \
	#  app_dfu_package.zip || exit 1
fi



#echo $NRFUTIL device list
#$NRFUTIL device list
K=`$NRFUTIL device list |  grep "^Ports" | awk '{print $2}'`
rc=$?
if [ $rc -ne 0 ]; then
	echo error # does not happen ?
	exit 1
fi
echo go $rc
echo P $K
if [ "$K" == "" ]; then
	echo no device
	exit 2
fi

$NRFUTIL dfu usb-serial -pkg app_dfu_package.zip -p $K
echo prog
#$NRFUTIL device program --firmware $H  --serial-number C909C8DEEB3C

