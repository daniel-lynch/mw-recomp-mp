#!/usr/bin/env bash
# Capture both cod4_mp windows on DISPLAY=:1 to $SHOT_DIR/<tag>_A.png and _B.png (A=leftmost window).
set -u
SP=${SHOT_DIR:-/tmp/cod4_mig_shots}; mkdir -p "$SP"
TAG=${1:-shot}
export DISPLAY=:1
# sort window ids by x position so [0]=A (x=5120,left), [1]=B (x=6400,right)
mapfile -t LINE < <(wmctrl -lG 2>/dev/null | grep -i cod4_mp | sort -k3 -n)
i=0
for tagside in A B; do
  ln="${LINE[$i]:-}"; i=$((i+1))
  [ -z "$ln" ] && { echo "no window for $tagside"; continue; }
  wid=$(echo "$ln" | awk '{print $1}')
  import -window "$wid" "$SP/${TAG}_${tagside}.png" 2>/dev/null && echo "captured ${TAG}_${tagside}.png ($wid)"
done
