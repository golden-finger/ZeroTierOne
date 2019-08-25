#!/bin/bash

if [ -z "$ZT_IDENTITY_PATH" ]; then
    ZT_IDENTITY_PATH="/var/lib/zerotier-one/identity"
fi

mkdir -p $ZT_IDENTITY_PATH
mkdir -p /var/lib/zerotier-one

pushd /var/lib/zerotier-one
ln -s $ZT_IDENTITY_PATH/identity.public identity.public
ln -s $ZT_IDENTITY_PATH/identity.secret identity.secret
popd

DEFAULT_PORT=9993
DEFAULT_NETWORK=1c33c1ced07dac19

#export GLIBCXX_FORCE_NEW=1
#export GLIBCPP_FORCE_NEW=1
#export LD_PRELOAD="/usr/lib64/libjemalloc.so"
/usr/local/bin/zerotier-one -d -p${ZT_CONTROLLER_PORT:-$DEFAULT_PORT} /var/lib/zerotier-one

NETWORK=${ZT_NETWOK:-$DEFAULT_NETWORK}

STATUS=""

zt_log() {

  echo "$(date) : $*"

}

while [ 1 -eq 1 ] ; do

  sleep 1
  /usr/local/bin/zerotier-cli listnetworks 2>/dev/null | grep ${NETWORK} 2>&1 1>/dev/null
  if [ $? -ne 0 ]; then
    if [ "x$STATUS" != "x" ]; then
      zt_log "network $NETWORK lost, try to reconnect ... "
      STATUS=""
    fi
    /usr/local/bin/zerotier-cli join ${NETWORK} 2>&1 1>/dev/null
    if [ $? -eq 0 ]; then
      STATUS="$(/usr/local/bin/zerotier-cli listnetworks | grep 1c33c1ced07dac19 | awk  '{for ( i=4; i<=NF; i++ )  printf " "$i; if (NF>2) printf"\n";}')"
      zt_log "network ${NETWORK} joined -> $STATUS"
    fi
  else
    net_desc="$(/usr/local/bin/zerotier-cli listnetworks | grep 1c33c1ced07dac19 | awk  '{for ( i=4; i<=NF; i++ )  printf " "$i; if (NF>2) printf"\n";}')"
    if [ "${STATUS}" != "${net_desc}" ]; then
      STATUS=$net_desc
      zt_log "status changed: $STATUS"
    fi
  fi

done
