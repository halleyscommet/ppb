#!/usr/bin/env bash
set -e

# Configuration
HOST="${PPB_HOST:-0.0.0.0}"
PORT="${PPB_PORT:-8000}"
WORKERS="${PPB_WORKERS:-4}"
LOG_LEVEL="${PPB_LOG_LEVEL:-info}"

echo "Starting PPB Server..."
echo "Host: $HOST"
echo "Port: $PORT"
echo "Workers: $WORKERS"
echo "Log Level: $LOG_LEVEL"

# Activate virtual environment
if [ -d ".venv" ]; then
    source .venv/bin/activate
fi

# Check if tokens.json exists
if [ ! -f "tokens.json" ]; then
    echo "Warning: tokens.json not found, creating with empty array"
    echo '[]' > tokens.json
fi

# Start Gunicorn
exec gunicorn \
    --bind "$HOST:$PORT" \
    --workers "$WORKERS" \
    --worker-class sync \
    --log-level "$LOG_LEVEL" \
    --access-logfile - \
    --error-logfile - \
    --timeout 120 \
    server:app