#!/usr/bin/env bash
# Fetch every tree-sitter grammar CodeSight supports (shallow clones).
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p third_party
cd third_party

clone() {
  if [ ! -d "$1" ]; then
    git clone --depth 1 "https://github.com/tree-sitter/$1.git"
  else
    echo "exists: $1"
  fi
}

clone tree-sitter
clone tree-sitter-python
clone tree-sitter-javascript
clone tree-sitter-c
clone tree-sitter-cpp
clone tree-sitter-go
clone tree-sitter-java
clone tree-sitter-c-sharp
clone tree-sitter-typescript
clone tree-sitter-rust
clone tree-sitter-ruby
clone tree-sitter-php

echo "All grammars present."
