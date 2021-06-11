#!/bin/bash -

export CXX=mpicxx
export CC=mpicc

### Use GCC 7 to use -fopenmp-simd
source /opt/rh/devtoolset-7/enable

cmake3 \
    -DCMAKE_CXX_FLAGS="-g -O2 -std=c++11 -DNEON -fopenmp -fopenmp-simd"  \
    -DCMAKE_INSTALL_PREFIX=/opt/chroma/double \
    -DMG_DEFAULT_LOGLEVEL=DEBUG3 \
    -DQMP_DIR="/opt/chroma/double/" \
    -DEigen3_DIR=/usr/include/eigen3 \
    -DCMAKE_BUILD_TYPE=Release \
    ../
