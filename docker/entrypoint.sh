#!/bin/bash

# Exit on error, real CI
set -e

echo "Initing openGauss..."

source /home/opengauss/.bashrc
source /home/opengauss/.wasmedge/env

source ~/.bashrc && gs_initdb -D /home/opengauss/openGauss/data/ -w "openGauss2022" -E utf8  --nodename=datanode 
sed -i "s/#listen_addresses = 'localhost'/listen_addresses = '*'/g"   /home/opengauss/openGauss/data/postgresql.conf
sed -i "s/#port = 5432/port = 5433/g" /home/opengauss/openGauss/data/postgresql.conf && \

echo "Starting openGauss..."
gs_ctl start -D /home/opengauss/openGauss/data -Z single_node

echo "Ready!"
if [[ ! -z $@ ]]; then
    echo
    echo "To connect to the database: "
    echo "  gsql -d postgres "
    echo
    $@
fi
