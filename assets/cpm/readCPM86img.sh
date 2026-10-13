mkdir -p cpm-read

dd if="cmp86.img" of="cpm-read/cmp86-data.img" bs=1 skip=11520

sed 's/offset 11520/offset 0/' diskdefs > cpm-read/diskdefs

(
  cd cpm-read
  cpmls -T raw -f comp86 -l "cmp86-data.img"
)
