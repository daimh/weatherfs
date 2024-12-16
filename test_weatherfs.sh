#!/usr/bin/env bash
set -xfEeuo pipefail
umount zipcode 2> /dev/null || :
mkdir -p zipcode
./weatherfs --conf=$(dirname $0)/test_weatherfs.json zipcode
cat zipcode/96701 # Hawaii
grep -w temp zipcode/99501 # Alaska
umount zipcode
