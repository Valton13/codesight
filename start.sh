#!/bin/bash

# Define colors for terminal output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}Starting CodeSight...${NC}"

# Create logs directory if it doesn't exist
mkdir -p logs

# 1. Start Python Backend
echo -e "${YELLOW}Starting Python Backend...${NC}"
cd python
source venv/bin/activate
python server.py > ../logs/python.log 2>&1 &
PYTHON_PID=$!
cd ..

# 2. Start React Frontend
echo -e "${YELLOW}Starting React Frontend...${NC}"
cd frontend
npm run dev > ../logs/frontend.log 2>&1 &
FRONTEND_PID=$!
cd ..

# 3. Start C++ Watcher
# Defaults to ./test_project if no directory argument is provided
WATCH_DIR=${1:-./test_project}
echo -e "${YELLOW}Starting C++ Watcher on $WATCH_DIR...${NC}"

if [ ! -f "./build/codesight" ]; then
    echo -e "${RED}Error: C++ binary not found. Please run 'cmake --build build' first.${NC}"
    kill $PYTHON_PID $FRONTEND_PID
    exit 1
fi

./build/codesight "$WATCH_DIR" > logs/watcher.log 2>&1 &
WATCHER_PID=$!

# Save Process IDs to a file for the stop script
echo "$PYTHON_PID" > .pids
echo "$FRONTEND_PID" >> .pids
echo "$WATCHER_PID" >> .pids

echo -e "${GREEN}All services started successfully!${NC}"
echo "Python Backend: http://127.0.0.1:8000"
echo "React Frontend: http://localhost:5173"
echo "Logs are saved in the ./logs directory."
echo -e "Run ${YELLOW}./stop.sh${NC} to stop all services."
