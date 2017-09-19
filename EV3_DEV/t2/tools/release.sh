#!/bin/bash

name="netlist_ev3_v1_14_release.tar"

# This executes "make clean" and creates a tar file of the source tree
# with the proper name

cd ..
make clean
cd app
make clean
cd ../tools
tar -cvf ../../$name ..

