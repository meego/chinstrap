#!/bin/bash 
cd ./emptyheaded
make clean 
make libs 
source ./libs/tbb43_20150424oss/bin/tbbvars.sh intel64 
make tests
