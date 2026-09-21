
const fs = require('fs');
const SRC = 'C:\\Users\\wu\\.dsh\\attachments\\v1\\files\\84\\8487adf5857549ba4a15a12b422663bb91fb17226c8ac029177d6b5fda2b4ea2\\lib20260914_082802.so';
const b = fs.readFileSync(SRC);

// 通配扫描 "ELF\x02\x01\x01" —— 找所有内嵌的 ELF 头
const hits = [];
for (let i = 0; i + 4 < b.length; i++) {
  if (b[i] === 0x7f && b[i+1] === 0x45 && b[i+2] === 0x4c && b[i+3] === 0x46) hits.push(i);
  if (hits.length > 400000) break;
}
console.log('ELF 魔数出现次数:', hits.length, '前 8 个 offset:', hits.slice(0,8).map(x=>'0x'+x.toString(16)).join(' '));

// 以第一个为模板校验其它是不是合法 ELF64
let good = [];
for (const h of hits) {
  if (h + 64 > b.length) continue;
  const cls = b[h+4], endian = b[h+5], ver = b[h+6];
  const etype = b.readUInt16LE(h+16), machine = b.readUInt16LE(h+18);
  if (cls === 2 && endian === 1 && ver === 1 && machine === 0xb7) good.push({h, etype});
  if (good.length >= 12) break;
}
console.log('合法 ELF64/aarch64 头:', good.length);
good.slice(0,8).forEach(g => console.log('  offset 0x' + g.h.toString(16), 'e_type=0x' + g.etype.toString(16), '(2=EXEC 3=DYN)'));

// 尾部区域找打包器元数据
const tailStart = b.length - 4096;
const tail = b.slice(tailStart).toString('latin1');
const markers = ['ELFPROT','ELFP','PROT','UPX','SecShell','naga','bangcle','ijiami','tencent','kiwi','Signature','signature'];
for (const m of markers) { const i = b.lastIndexOf(m); if (i >= 0) console.log('marker', m, '@0x' + i.toString(16)); }

// 读文件前 400 字节里可能的 ASCII（有些壳把元信息放头里）
const head = b.slice(0, 512).toString('latin1').replace(/[^\x20-\x7e]/g, '.');
console.log('head ASCII:', head.slice(0, 200));
