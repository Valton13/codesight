#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

mkdir -p logs

VENV=""
for cand in "$ROOT/python/venv" "$ROOT/venv"; do
  if [ -x "$cand/bin/python" ]; then VENV="$cand"; break; fi
done
if [ -z "$VENV" ]; then
  echo -e "${YELLOW}Creating python/venv and installing requirements...${NC}"
  python3 -m venv "$ROOT/python/venv"
  VENV="$ROOT/python/venv"
  "$VENV/bin/pip" install --quiet -r "$ROOT/python/requirements.txt"
fi
PY="$VENV/bin/python"

echo -e "${GREEN}Starting CodeSight...${NC}"

echo -e "${YELLOW}Starting Python Backend...${NC}"
cd "$ROOT/python"
"$PY" server.py > "$ROOT/logs/python.log" 2>&1 &
BACKEND_PID=$!
cd "$ROOT"

echo -e "${YELLOW}Starting React Frontend...${NC}"
cd "$ROOT/frontend"
npm run dev > "$ROOT/logs/frontend.log" 2>&1 &
FRONTEND_PID=$!
cd "$ROOT"

WATCH_DIR="${1:-$ROOT/test_project}"
if [ ! -x "$ROOT/build/codesight" ]; then
  echo -e "${RED}C++ binary missing. Run: cmake --build build -j\$(nproc)${NC}"
  kill "$BACKEND_PID" "$FRONTEND_PID" 2>/dev/null || true
  exit 1
fi
echo -e "${YELLOW}Starting C++ Watcher on $WATCH_DIR...${NC}"
"$ROOT/build/codesight" "$WATCH_DIR" > "$ROOT/logs/watcher.log" 2>&1 &
WATCHER_PID=$!

printf '%s\n' "$BACKEND_PID" "$FRONTEND_PID" "$WATCHER_PID" > "$ROOT/.pids"

ok=0
for _ in $(seq 1 40); do
  if curl -sf http://127.0.0.1:8000/ >/dev/null 2>&1; then ok=1; break; fi
  sleep 0.5
done
if [ "$ok" != 1 ]; then
  echo -e "${RED}Backend failed to start. Last log lines:${NC}"
  tail -n 20 "$ROOT/logs/python.log" || true
  "$ROOT/stop.sh" || true
  exit 1
fi

echo -e "${GREEN}All services started successfully!${NC}"
echo "Python Backend: http://127.0.0.1:8000"
echo "React Frontend: http://localhost:5173"
echo "Logs are saved in the ./logs directory."
echo -e "Run ${YELLOW}./stop.sh${NC} to stop all services."
