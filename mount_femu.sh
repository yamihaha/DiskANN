sudo mkdir -p /mnt/ssd
sudo mkfs.ext4 /dev/nvme0n1
sudo mount /dev/nvme0n1 /mnt/ssd
lsblk /dev/nvme0n1
sudo chmod -R 777 /mnt/ssd