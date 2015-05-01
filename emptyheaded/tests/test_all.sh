#!/bin/bash 

#assumes this is called from EH folder
cd ./emptyheaded
source ./libs/build.sh
source ./libs/tbbvars.sh intel64
make libs
make tests
make libs
export PATH=$PATH:/home/travis/build/craberger/chinstrap/emptyheaded/libs/zeromq-4.0.5/include
export CPATH=$CPATH:/home/travis/build/craberger/chinstrap/emptyheaded/libs/zeromq-4.0.5/include
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/travis/build/craberger/chinstrap/emptyheaded/libs/zeromq-4.0.5/include
export LIBRARY_PATH=LIBRARY_PATH:/home/travis/build/craberger/chinstrap/emptyheaded/libs/zeromq-4.0.5/include
make server
./bin/server &
cd ../duncecap
./binariesForTravis/sbt-for-travis test
cd ..
