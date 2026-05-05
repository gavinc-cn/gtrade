set -eu
cd $(dirname `realpath "$0"`)

cd ~/git/gtrade
git pull

cd release
time python build.py --mode prod

#mkdir -p ~/gtrade
#cp -rf ~/git/gtrade/build/gtrade_prod/* ~/gtrade