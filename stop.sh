#!/bin/bash

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

if [ ! -f .pids ]; then
    echo -e "${RED}No running services found (missing .pids file).${NC}"
    exit 1
fi

echo -e "${RED}Stopping CodeSight services...${NC}"

# Read PIDs and kill them
while read pid; do
    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid"
        echo "Stopped process $pid"
    else
        echo "Process $pid was already stopped."
    fi
done < .pids

# Clean up the PID file
rm .pids
echo -e "${GREEN}All services stopped.${NC}"
