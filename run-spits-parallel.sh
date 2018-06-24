#!/bin/bash

############################################
# This is the OpenMP run for the RayTracer #
############################################

# You can either pass scenes individually as arguments or 
# leave it blank to run all scenes in the 'scenes' folder
SCENES="${@:1}"
SCENES="${SCENES:-`ls scenes/*.scn`}"

# Program Folder
export PF=`pwd`

# Ray tracing parameters
export SUPER_SAMPLES=1
export DEPTH_COMPLEXITY=5

# Runtime directory
export PROGDIR="./runtime"
export PROGRAM="$PROGDIR/spitz-run.sh"

# Input binary
export MODULEDIR="./bin"
export MODULE="$MODULEDIR/RayTracer_spits-$HOSTNAME.so"

# Output directory
export OUTDIR="./results/spits-parallel-$HOSTNAME"

# Execution directory
export RUNDIR="./run/$HOSTNAME/"

# Ensure the output directory exists
mkdir -p $OUTDIR

# Ensure the execution directory exists
if [ ! -d "$RUNDIR" ]; then
    mkdir -p $RUNDIR
    cd $RUNDIR
    ln -s ../../models/ . 
    cd -
fi

# Some PYPITS flags
#
# --overfill is the number of extra tasks sent to each Task Manager 
#            to make better use of the TCP connection, usually a 
#            value a few times larger than the number of workers 
#            works best, this also avoids starvation problems
#
export PPFLAGS="--overfill=20"

# Iterate through selected scenes and 
# run the ray tracer using time 
for SCENE in $SCENES
do
    FILENAME="$OUTDIR/`basename $SCENE`"
    FILENAME="${FILENAME%.*}"
    OUTFILE="$FILENAME.tga"
    OUTLOG="$FILENAME.log"
    OUTDIR="$RUNDIR/`basename $SCENE`"
    echo $SCENE...
    mkdir -p $OUTDIR
    pushd $OUTDIR
    rm -rf nodes* jm.* log
    (time $PF/$PROGRAM $PPFLAGS $PF/$MODULE \
    $PF/$SCENE $SUPER_SAMPLES $DEPTH_COMPLEXITY $PF/$OUTFILE) 2>&1 | tee $PF/$OUTLOG
    popd
done
