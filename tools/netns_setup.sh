#!/usr/bin/env bash
# One-time root setup for two-instance "two real machines" networking.
# Run:  sudo bash tools/netns_setup.sh
#
# Creates two network namespaces (ns_a, ns_b) joined by a veth pair, each with its OWN loopback (127.0.0.1)
# and a distinct LAN IP (10.0.0.1 / 10.0.0.2). Two game instances launched one-per-namespace then look like
# two separate machines: each binds the REAL Xbox UDP ports (no same-box conflict), the title's
# 127.0.0.1=="self" logic works per-namespace, and peers are reached directly at 10.0.0.x (no NAT/GAMEROUTE).
#
# Also installs a scoped sudoers rule so the (non-root) test harness can launch a process inside ns_a/ns_b
# without a password each time. NOTE: `ip netns exec ns_a <cmd>` runs <cmd> as root in that namespace, so
# this effectively grants the user root for commands run through it — acceptable on a dev box; remove
# /etc/sudoers.d/cod4_netns to revoke.
set -e
USER_NAME="${SUDO_USER:-dlynch}"

for ns in ns_a ns_b; do ip netns del "$ns" 2>/dev/null || true; done
ip link del veth_a 2>/dev/null || true

ip netns add ns_a
ip netns add ns_b
ip link add veth_a type veth peer name veth_b
ip link set veth_a netns ns_a
ip link set veth_b netns ns_b
ip netns exec ns_a ip addr add 10.0.0.1/24 dev veth_a
ip netns exec ns_a ip link set veth_a up
ip netns exec ns_a ip link set lo up
ip netns exec ns_b ip addr add 10.0.0.2/24 dev veth_b
ip netns exec ns_b ip link set veth_b up
ip netns exec ns_b ip link set lo up

echo "netns ready: ns_a=10.0.0.1 ns_b=10.0.0.2"
if ip netns exec ns_a ping -c1 -W2 10.0.0.2 >/dev/null 2>&1; then
  echo "veth connectivity OK (ns_a -> ns_b)"
else
  echo "!! veth PING FAILED — check the setup"
fi

cat > /etc/sudoers.d/cod4_netns <<EOF
$USER_NAME ALL=(root) NOPASSWD: /usr/sbin/ip netns exec ns_a *
$USER_NAME ALL=(root) NOPASSWD: /usr/sbin/ip netns exec ns_b *
EOF
chmod 0440 /etc/sudoers.d/cod4_netns
echo "passwordless 'sudo ip netns exec ns_a|ns_b ...' enabled for $USER_NAME"
echo "DONE. Now run the test harness as your normal user: bash tools/run_mig_netns.sh"
