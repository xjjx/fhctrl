#!/bin/sh

export VST_PATH='/home/xj/Muzyka/VST'
#export XTERM='gnome-terminal -e'
export XTERM='xterm -e'
#export PATH="/home/xj/Muzyka/fsthost/trunk:/home/xj/Muzyka/fhctrl:$PATH"
export PATH="/home/studio/SVN/fhctrl:/media/bigpig/secure/XJ/lvst/fsthost/trunk:$PATH"

UUID=0

MODE=$1
SES_PATH=$2

usage() {
	echo "Usage: $0 load|save SessionPath"
	exit 1
}

[ -z "$MODE" -o -z "$SES_PATH" ] && usage

if [ ! -d "$SES_PATH" ] && ! mkdir "$SES_PATH"; then
	echo "Session Directory does not exists, and can't create it $SES"
	exit 1
fi

case $MODE in
	'load')
		. ${SES_PATH%/}/session.sh
		jobs -l
		wait
		;;
	'save')
		fhctrl_sn save $SES_PATH > $SES_PATH/session.sh
		echo 'Session Saved'
		;;
	*) usage
esac
