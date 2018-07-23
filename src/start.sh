# Code to be executed by the virtual machine only !!!
if [[ `uname -r` != "4.9.83" ]]; then
  echo That Script must be launched with virtual machine only
  exit 1
fi
loadkeys fr
if [ -d /root/mnt/ ]; then
  rmdir /root/mnt/
fi
mkdir /root/mnt/ &&
cp /root/share/src/disk.img /tmp &&
insmod /root/share/src/pnlfs.ko && 
mount -t pnlfs -o loop /tmp/disk.img /root/mnt/