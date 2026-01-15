#!/bin/bash

if [ -f ../pandaPIParser ]; then
    echo "pandaPIgrounder already compiled - skipping build"
    exit
fi

# Fetch a clean state of pandaPIparser
if [ ! -d pandaPIparser ]; then
    echo "Fetching pandaPIparser ..."
    git clone https://github.com/panda-planner-dev/pandaPIparser.git
    cd pandaPIparser
else
    cd pandaPIparser
    git clean -f
fi

# Checkout correct commit (can be updated but must be manually checked to build cleanly)
git config advice.detachedHead false
# git checkout 95bbe291c5bdb9fb517c1ad55f5136d45450c644
git checkout 334393290c13089a1a7e0ced070cc272f76fedf2

# Build original standalone executable for debugging purposes
make -j

# Copy the executable to the lib folder
cp pandaPIparser ../../