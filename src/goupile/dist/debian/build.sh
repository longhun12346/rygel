#!/bin/sh -e

cd /io

# Fix git error about dubious repository ownership
git config --global safe.directory '*'

./bootstrap.sh --no_user
./felix -pFast --no_user -O bin/Packages/goupile/debian/bin goupile
