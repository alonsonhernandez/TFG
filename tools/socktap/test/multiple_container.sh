#!/bin/bash

# Show help message if requested
if [[ "$1" == "--help" ]]; then
  echo "Usage: $0 [options]"
  echo ""
  echo "Options:"
  echo "  --containers N     Number of containers to launch (default: 1)"
  echo "  --update           Rebuild the Docker image before launching"
  echo "  --terminals        Open a terminal per container"
  echo "  --logs             Copy logs from containers after 5 minutes"
  echo "  --logdir PATH      Directory to store the log files (default: ~/Documents)"
  echo "  --help             Show this help message"
  exit 0
fi

# Parse arguments
PARSED=$(getopt --options="" --longoptions=containers:,update,terminals,logs,logdir: --name "$0" -- "$@")
eval set -- "$PARSED"

# Default values
CONTAINERS=1
UPDATE=false
TERMINALS=false
LOGS=false
LOGDIR="$HOME/Documents"

# Read options
while true; do
  case "$1" in
    --containers)
      CONTAINERS="$2"
      shift 2
      ;;
    --update)
      UPDATE=true
      shift
      ;;
    --terminals)
      TERMINALS=true
      shift
      ;;
    --logs)
      LOGS=true
      shift
      ;;
    --logdir)
      LOGDIR="$2"
      shift 2
      ;;
    --)
      shift
      break
      ;;
    *)
      echo "Invalid option: $1"
      exit 1
      ;;
  esac
done

# Create log directory if it doesn't exist
mkdir -p "$LOGDIR"

# Go to the project directory
cd /home/yeray/proyectos/mcoVanetza/tools/socktap/test || exit 1

# Stop existing containers
for ((i = 1; i <= CONTAINERS; i++)); do
  sudo docker stop socktap$i 2>/dev/null || echo "Container socktap$i was not running"
done

# Remove old containers
for ((i = 1; i <= CONTAINERS; i++)); do
  sudo docker rm socktap$i 2>/dev/null || echo "No container socktap$i to remove"
done

# Rebuild Docker image if requested
if $UPDATE; then
  sudo ./conteneriza.sh
fi

# Calculate sleep time: 100ms / number of containers
SLEEP_TIME=$(awk "BEGIN {print 0.1 / $CONTAINERS}")

# Launch containers
if [ "$CONTAINERS" == "1" ]; then
  sudo docker run -ti -v /usr/local/src/socktap1:/usr/local/src/socktap --name socktap --network bridge socktap-docker
else
  for ((i = 1; i <= CONTAINERS; i++)); do
    sudo sleep "$SLEEP_TIME"
    sudo docker run -d -v /usr/local/src/socktap$i:/usr/local/src/socktap --name socktap$i --network bridge socktap-docker
  done

  # Open terminals for logs if requested
  if $TERMINALS; then
    for ((i = 1; i <= CONTAINERS; i++)); do
      gnome-terminal --tab --title="Socktap Terminal $i" -- bash -c "sudo docker logs -f socktap$i; exec bash"
    done
  fi
fi

# Copy logs after 5 minutes if requested
if $LOGS; then
  echo "Waiting 5 minutes before copying logs..."
  sudo sleep 300
  for ((i = 1; i <= CONTAINERS; i++)); do
    sudo docker cp socktap$i:/home/build-user/inpercept_log.log "$LOGDIR/inpercept$i.log"
  done
  echo "Logs copied to: $LOGDIR"
fi
