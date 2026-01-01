#!/bin/bash
# create_project_structure.sh

PROJECT_NAME="meta-quest3s-lab"
PROJECT_DIR="$HOME/repos/$PROJECT_NAME"

mkdir -p "$PROJECT_DIR"/{src,include,shaders,assets,build,libs}
mkdir -p "$PROJECT_DIR"/src/{core,xr,rendering,utils}
mkdir -p "$PROJECT_DIR"/include/{core,xr,rendering,utils}

cd "$PROJECT_DIR"

echo "Project structure created at: $PROJECT_DIR"
tree -L 2