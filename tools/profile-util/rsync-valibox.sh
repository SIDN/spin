#!/bin/sh
#
# rsync.sh: upload the Lua scripts and related files to the Valibox.
#
# Tip: put your SSH public key in /etc/dropbear/authorized_keys on the Valibox.
#

set -e

SSH="ssh root@192.168.8.1"
DIR="/root/caspar-lua"

${SSH} mkdir -p "${DIR}"
for f in db.schema enum.lua enums.lua fw.lua generate-fw.lua generate-profile.lua profile.lua mqtt_nm.lua nm.lua util_validate.lua ; do
	if ! ${SSH} "cmp -s "${DIR}/${f}" -" <"${f}" ; then
		echo "Updating ${f}"
		${SSH} "cat >"${DIR}/${f}"" <"${f}"
	else
		echo "${f} is up to date"
	fi
done

