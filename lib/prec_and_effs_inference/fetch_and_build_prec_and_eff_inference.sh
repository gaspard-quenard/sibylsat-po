#!/bin/bash

if [ -f ../pandaPIengine ]; then
    echo "pandaPIengine already compiled - skipping build"
    exit
fi

# Fetch a clean state of pandaPIparser
if [ ! -d pandaPIparser ]; then
    echo "Fetching pandaPIengine ..."
    git clone https://github.com/panda-planner-dev/pandaPIengine
    cd pandaPIengine
else
    cd pandaPIengine
    git clean -f
fi

# Checkout correct commit (can be updated but must be manually checked to build cleanly)
git config advice.detachedHead false
git checkout 810f04388667db5e3e4f114e960a4efbb43b1ac0

# Apply the patch to added the function to infer the preconditions and effects
# of the methods and the option to do so
git apply ../added_precs_and_effs.patch

# Build original standalone executable
mkdir build
cd build
cmake ../src
make -j

# Copy the executable to the lib folder
cp pandaPIengine ../../../