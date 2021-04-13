#!/bin/sh
cat out | sed -n -e '/>>>start/,/===/p' > $1
sed -i '/^.*>>>start/d' $1
sed -i '/^===/d' $1
sed -i '/^WARNING/d' $1
