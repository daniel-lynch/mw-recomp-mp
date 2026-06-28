#!/usr/bin/env bash
# run_mig.sh with bots on host A (so A fills + plays the match). Thin wrapper — sets the A-side bot/probe
# env and delegates to the robust run_mig.sh (cleanup + launch-verify-retry + unique fifos).
#   MIG_A_EXTRA_MORE="COD4_MM_DVARSCAN=1 ..."  to add probe flags on A.
export MIG_A_EXTRA="COD4_GSCINJECT=1 COD4_BOTSPAWN=1 COD4_BOTAI=1 COD4_BOTSETTLE=120 ${MIG_A_EXTRA_MORE:-}"
exec bash /home/dlynch/dev/mw-recomp-mp/tools/run_mig.sh
