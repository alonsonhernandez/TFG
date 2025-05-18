#!/bin/bash

# Default values
NUM_CONTAINERS=1
REMOVE_CONTAINERS=false

# Help message
print_help() {
    echo "Usage: sudo ./eliminate_container.sh [options]"
    echo
    echo "Options:"
    echo "  --containers N     Number of containers to stop (default: 1)"
    echo "  --remove           Also remove the containers after stopping them"
    echo "  --help             Show this help message"
    exit 0
}

# Parse arguments
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --containers)
            NUM_CONTAINERS="$2"
            shift
            ;;
        --remove)
            REMOVE_CONTAINERS=true
            ;;
        --help)
            print_help
            ;;
        *)
            echo "Unknown option: $1"
            print_help
            ;;
    esac
    shift
done

# Stop (and optionally remove) containers
for ((i = 1; i <= NUM_CONTAINERS; i++)); do
    echo "Stopping container: socktap$i"
    sudo docker stop socktap$i || echo "No containers found with the name socktap$i"

    if [ "$REMOVE_CONTAINERS" = true ]; then
        echo "Removing container: socktap$i"
        sudo docker rm socktap$i || echo "Failed to remove container socktap$i"
    fi
done


