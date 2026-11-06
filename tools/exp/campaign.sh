#!/usr/bin/env bash
# Bare-metal A/B driver for compiled kernel variants.
#
# This script is the only part of the campaign that must survive a reboot, so it
# is written to be idempotent: every step records its state under
# .logs/exp/state/ before it runs, and a resumed invocation picks up from the
# last completed step.
#
#   campaign.sh plan                 show the queue and the current position
#   campaign.sh dry-run              print what the next step would do
#   campaign.sh resume               run the next step (called by the systemd unit)
#   campaign.sh init                 reset the queue position
#
# Queue file: .logs/exp/campaign.tsv, one line per variant:
#   <label>\t<bzImage path>\t<rounds>
#
# State file: .logs/exp/state/campaign.json
set -uo pipefail

EXP_DIR="${EXP_DIR:-$HOME/Projects/linux-catchya/.logs/exp}"
QUEUE="$EXP_DIR/campaign.tsv"
STATE="$EXP_DIR/state/campaign.json"
CAMPAIGN_LOG="$EXP_DIR/raw/campaign.log"
KEXEC_CMDLINE_EXTRA="panic=10 softlockup_panic=1 hung_task_panic=1"

mkdir -p "$EXP_DIR/state" "$EXP_DIR/raw"

log() { echo "[$(date -Is)] $*" | tee -a "$CAMPAIGN_LOG"; }

read_state() { [ -f "$STATE" ] && jq -r '.step // 0' "$STATE" || echo 0; }
write_state() { jq -n --argjson step "$1" --arg kernel "$(uname -r)" \
	'{step: $step, kernel: $kernel, at: (now|todate)}' > "$STATE"; }

cmd_plan() {
	[ -f "$QUEUE" ] || { echo "no queue at $QUEUE"; exit 1; }
	local i=0 step; step=$(read_state)
	while IFS=$'\t' read -r label image rounds; do
		[ -z "${label:-}" ] && continue
		marker="   "; [ "$i" -lt "$step" ] && marker="done"; [ "$i" -eq "$step" ] && marker="next"
		printf '%-4s %-2s %-20s %s (%s rounds)\n' "$marker" "$i" "$label" "$image" "${rounds:-10}"
		i=$((i + 1))
	done < "$QUEUE"
}

next_entry() {
	local step; step=$(read_state)
	awk -F'\t' -v want="$step" '
		/^#/ || NF < 2 { next }
		{ if (n == want) { print; exit } n++ }
	' "$QUEUE"
}

cmd_resume() {
	local entry; entry=$(next_entry)
	if [ -z "$entry" ]; then
		log "campaign complete; results under $EXP_DIR/raw/"
		return 0
	fi
	IFS=$'\t' read -r label image rounds <<< "$entry"
	rounds="${rounds:-10}"
	log "step $label: kernel running is $(uname -r)"

	# 1. Measure this kernel if it is the intended one.
	if [ "$(uname -r)" = "$label" ]; then
		log "measuring $label for $rounds rounds"
		"$HOME/Projects/linux-catchya/tools/exp/bench.sh" --tag "$label" \
			--out "$EXP_DIR/raw/campaign-$label-full.json" >> "$CAMPAIGN_LOG" 2>&1
		# Bump the queue position only after the measurement is on disk.
		local step; step=$(read_state)
		write_state $((step + 1))
		log "step $label complete; advanced to $((step + 1))"
		return 0
	fi

	# 2. Otherwise boot it.
	if [ ! -f "$image" ]; then
		log "ERROR: $image missing; skipping $label"
		local step; step=$(read_state)
		write_state $((step + 1))
		return 1
	fi
	local cmdline
	cmdline="$(cat /proc/cmdline) $KEXEC_CMDLINE_EXTRA"
	log "loading $image"
	if ! kexec -l "$image" --initrd=/boot/initramfs-linux-catchya.img --command-line="$cmdline"; then
		log "ERROR: kexec load failed; skipping $label"
		local step; step=$(read_state)
		write_state $((step + 1))
		return 1
	fi
	log "kexec into $label"
	kexec -e
}

case "${1:-plan}" in
	plan)    cmd_plan ;;
	dry-run) cmd_plan; entry=$(next_entry); echo "next: ${entry:-<complete>}" ;;
	resume)  cmd_resume ;;
	init)    write_state 0; log "queue reset" ;;
	*) echo "usage: $0 plan|dry-run|resume|init" >&2; exit 2 ;;
esac
