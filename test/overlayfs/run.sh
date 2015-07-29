#!/bin/bash

set -eu

CRIU=../../../criu

ERROR=0

setup() {
	setup_mount
	start_python > /dev/null 2>&1 &
	BASH_PID=$!
	PYTHON_PID=$(ps -C python | awk '/python/ { print $1 }')
	echo "BASH_PID=$BASH_PID PYTHON_PID=$PYTHON_PID"
	sleep 1
}

setup_mount() {
	mkdir -p overlay_test
	cd overlay_test
	mkdir -p a b c z checkpoint
	sudo mount -t overlay -o lowerdir=a,upperdir=b,workdir=c overlayfs z
}

start_python() {
	python << EOF
import time
fd = open("z/file", "w")
time.sleep(10)
EOF
}

check_criu() {
	echo "Dumping $PYTHON_PID..."
	sudo $CRIU dump -v4 -D checkpoint --shell-job --overlayfs -t $PYTHON_PID
	if [[ $? -ne 0 ]]; then
		ERROR=1
		echo "ERROR! dump failed"
	fi

	echo "Restoring..."
	sudo $CRIU restore -d -D checkpoint --shell-job
	if [[ $? -ne 0 ]]; then
		ERROR=1
		echo "ERROR! restore failed"
	fi
}

finish() {
	kill -INT $PYTHON_PID > /dev/null 2>&1
	sudo umount z
	cd $ORIG_WD
	rm -rf overlay_test
}

main() {
	ORIG_WD=$(pwd)
	setup

	check_criu

	finish
	[[ $ERROR -eq 0 ]] && echo "OverlayFS C/R successful."
}

main
