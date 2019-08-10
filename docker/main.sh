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

#export GLIBCXX_FORCE_NEW=1
#export GLIBCPP_FORCE_NEW=1
#export LD_PRELOAD="/usr/lib64/libjemalloc.so"
exec /usr/local/bin/zerotier-one -p${ZT_CONTROLLER_PORT:-$DEFAULT_PORT} /var/lib/zerotier-one
