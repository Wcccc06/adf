#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""setvermagic.py - 把 .ko 里的 vermagic 改成与运行内核一致（设备实测的硬门槛）

用法:
    python3 setvermagic.py adf_driver.ko 6.1.118-android14-11-o-g64180ab070e5

原理:
    vermagic 存放在 .modinfo 段的 'vermagic=<字符串>' 里，内核 check_version() 会比对 release。
    替换必须等长（原地覆盖），短了用 NUL 填充，长了就报错并给出手工方案。
"""
import sys
import re

MAGIC = b"vermagic="


def find_vermagic(data):
    out = []
    start = 0
    while True:
        i = data.find(MAGIC, start)
        if i < 0:
            break
        j = data.find(b'\x00', i)
        if j < 0:
            break
        out.append((i, j, data[i + len(MAGIC):j]))
        start = j
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    path, newrel = sys.argv[1], sys.argv[2]
    with open(path, 'rb') as f:
        data = bytearray(f.read())

    hits = find_vermagic(bytes(data))
    if not hits:
        print('[!] 没找到 vermagic 段，可能不是标准内核模块')
        return 1

    changed = False
    for off, end, old in hits:
        olds = old.decode('utf-8', 'replace')
        m = re.match(r'^(\S+)(\s+.*)?$', olds)
        if not m:
            continue
        tail = m.group(2) or ''
        newvm = newrel + tail
        newbytes = newvm.encode()
        oldlen = end - (off + len(MAGIC))
        print('[*] 原 vermagic : ' + olds)
        print('[*] 新 vermagic : ' + newvm)
        if len(newbytes) > oldlen:
            print('[!] 新串更长 (%d > %d)，无法原地替换' % (len(newbytes), oldlen))
            print('    手工方案: 编译时用运行内核 release 生成，或覆盖 KBUILD_VERMAGIC')
            return 1
        data[off + len(MAGIC):end] = newbytes + b'\x00' * (oldlen - len(newbytes))
        changed = True

    if not changed:
        print('[=] 无需修改')
        return 0

    with open(path, 'wb') as f:
        f.write(data)
    print('[+] 已写回 ' + path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
