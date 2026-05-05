set -eu
cd $(dirname `realpath "$0"`)

mode=prod

this_dir=$(pwd)
t=$(date +%Y%m%d_%H%M%S)
tmp_dir=/tmp/gtrade_download
install_dir=~/gtrade
bak_name=gtrade.bak${t}
zip_name=$(basename $(ls ${this_dir}/GTRADE_${mode^^}_*.zip | sort | tail -n1) .zip)

#echo -- clean tmp dir
#rm -rf /tmp/gtrade_download
#mkdir -p ${tmp_dir}

#echo -- download zip from remote
#rsync -av datasvc@192.168.168.132:/path/to/zip/file/GTRADE_${mode^^}_*.zip ${tmp_dir}

echo -- bakup old
mv gtrade bak/${bak_name}

echo -- install new
unzip ${zip_name}.zip
mv ${zip_name} ${install_dir}
rm ${zip_name}.zip

echo -- zip bak files
cd bak
zip -r ${bak_name}.zip ${bak_name}
rm -r ${bak_name}
