#!/bin/bash
# Simple confirmation prompt helper

printf "%s (y/N) " "$1"
read -r REPLY
case "$REPLY" in
  [yY]|[yY][eE][sS])
    exit 0
    ;;
  *)
    echo "Cancelled."
    exit 1
    ;;
esac
