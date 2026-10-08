#!/usr/bin/env bash
# Plays Breakout headless through the jm driver, reacting to the game's state.
# Writes the transcript to stdout; the run's inputs to $1 (a replay file).
set -euo pipefail
REC=${1:-playthrough.replay.txt}
rm -rf /tmp/pt-save && mkdir -p /tmp/pt-save
coproc GAME { JM_DRIVE=1 JM_DRIVE_RECORD="$REC" JM_RENDERER=none JM_SAVE_DIR=/tmp/pt-save jm run 2>/dev/null; }

send() {  # send a command, print it and its (possibly trimmed) answer
  echo "$1" >&"${GAME[1]}"
  read -r REPLY <&"${GAME[0]}"
  echo "> $1"
  echo "${REPLY:0:400}"
}
quiet() { echo "$1" >&"${GAME[1]}"; read -r REPLY <&"${GAME[0]}"; }
num() { sed -n "s/.*\"$1\":\(-\{0,1\}[0-9.]*\).*/\1/p" <<<"$REPLY" | head -1; }

read -r REPLY <&"${GAME[0]}"; echo "$REPLY"
echo "## Title screen"
send "step 30"
send "state scene ui"
echo "## Enter starts the game"
send "press Enter"
send "step 2"
send "state scene session"
echo "## Arrow keys move the paddle"
send "state tag=Paddle"; echo "paddle x = $(num x)"
send "down ArrowRight"; send "step 20"; send "up ArrowRight"; send "step 1"
send "state tag=Paddle"; echo "paddle x after 20 frames of ArrowRight = $(num x)"
send "down ArrowLeft"; send "step 40"; send "up ArrowLeft"; send "step 1"
send "state tag=Paddle"; echo "paddle x after 40 frames of ArrowLeft = $(num x)"

echo "## Tracking the ball with the paddle for 1500 frames (keys chosen from state each 4 frames)"
held=""
for ((i = 0; i < 375; i++)); do
  quiet "state tag=Ball"; bx=$(num x)
  quiet "state tag=Paddle"; px=$(num x)
  want=""
  if awk -v b="$bx" -v p="$px" 'BEGIN{exit !(b > p + 30)}'; then want=ArrowRight; fi
  if awk -v b="$bx" -v p="$px" 'BEGIN{exit !(b < p - 30)}'; then want=ArrowLeft; fi
  if [[ "$want" != "$held" ]]; then
    [[ -n "$held" ]] && quiet "up $held"
    [[ -n "$want" ]] && quiet "down $want"
    held=$want
  fi
  quiet "step 4"
  if (( i % 75 == 74 )); then send "state session"; fi
done
[[ -n "$held" ]] && send "up $held"

echo "## Hands off: the ball falls past the paddle and lives go down"
for ((i = 0; i < 12; i++)); do
  send "step 120"
  send "state scene session"
  if grep -q gameover <<<"$REPLY"; then break; fi
done
echo "## Game over screen"
send "state ui"
echo "## Enter goes back to the title"
send "press Enter"
send "step 2"
send "state scene"
echo "## Final state (for the replay check)"
send "state scene session tag=Paddle"
FINAL=$REPLY
send "quit"
echo "$FINAL" > /tmp/pt-final.json
