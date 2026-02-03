#!/bin/bash
# Download required header-only libraries

set -e

echo "Setting up dependencies..."

# Create include directory
mkdir -p include/ankerl

# Download unordered_dense
if [ ! -f "include/ankerl/unordered_dense.h" ]; then
    echo "Downloading ankerl/unordered_dense..."
    curl -L -o include/ankerl/unordered_dense.h \
        https://raw.githubusercontent.com/martinus/unordered_dense/v4.8.1/include/ankerl/unordered_dense.h
    echo "✓ unordered_dense.h downloaded"
else
    echo "✓ unordered_dense.h already exists"
fi

# Download stl.h
if [ ! -f "include/ankerl/stl.h" ]; then
    echo "Downloading ankerl/stl.h..."
    curl -L -o include/ankerl/stl.h \
        https://raw.githubusercontent.com/martinus/unordered_dense/v4.8.1/include/ankerl/stl.h
    echo "✓ stl.h downloaded"
else
    echo "✓ stl.h already exists"
fi

echo "Setup complete!"
