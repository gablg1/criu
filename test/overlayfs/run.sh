#!/bin/bash

set -eu

CRIU=../../../criu

setup() {
	setup_mount
	sleep 10 3>z/file &
	PROC_PID=$!
	echo "PROC_PID=$PROC_PID"
	sleep 1
}

setup_mount() {
	mkdir -p overlay_test
	cd overlay_test
	mkdir -p a b c z checkpoint
	sudo mount -t overlay -o lowerdir=a,upperdir=b,workdir=c overlayfs z
}

check_criu() {
	echo "Dumping $PROC_PID..."
	if ! sudo $CRIU dump -D checkpoint --overlayfs --shell-job -t "${PROC_PID}"; then
		echo "ERROR! dump failed"
		return 1
	fi

	echo "Restoring..."
	if ! sudo $CRIU restore -d -D checkpoint --shell-job; then
		echo "ERROR! restore failed"
		return 1
	fi
	return 0
}

finish() {
	kill -INT "${PROC_PID}" > /dev/null 2>&1
	sudo umount z
	cd "${ORIG_WD}"
	rm -rf overlay_test
}

main() {
	ORIG_WD=$(pwd)
	setup

	check_criu || (finish && exit 1)

	finish
	echo "OverlayFS C/R successful."
	exit 0
}

main
