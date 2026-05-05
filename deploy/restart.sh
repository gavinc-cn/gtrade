set -eu

bash stop.sh
sleep 1
bash start.sh
sleep 1
bash status.sh

