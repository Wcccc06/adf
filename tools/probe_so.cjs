
const fs = require('fs');
const P = 'C:\\Users\\wu\\.dsh\\attachments\\v1\\files\\84\\8487adf5857549ba4a15a12b422663bb91fb17226c8ac029177d6b5fda2b4ea2\\lib20260914_082802.so';
const b = fs.readFileSync(P);
console.log('size', b.length);

// 通用：解析 ELF64 的 .dynsym / .dynstr / .rodata
const shoff = Number(b.readBigUInt64LE(40)), shentsize = b.readUInt16LE(58), shnum = b.readUInt16LE(60), shstrndx = b.readUInt16LE(62);
const so = shoff + shstrndx * shentsize, shstr = Number(b.readBigUInt64LE(so + 24));
const secs = [];
for (let i = 0; i < shnum; i++) {
  const o = shoff + i * shentsize;
  const noff = b.readUInt32LE(o);
  let nm = ''; for (let k = shstr + noff; b[k]; k++) nm += String.fromCharCode(b[k]);
  secs.push({ nm, off: Number(b.readBigUInt64LE(o + 24)), size: Number(b.readBigUInt64LE(o + 32)), entsz: Number(b.readBigUInt64LE(o + 56)) });
}
const dynsym = secs.find(s => s.nm === '.dynsym');
const dynstr = secs.find(s => s.nm === '.dynstr');
console.log('sections:', secs.map(s => s.nm + '(' + s.size + ')').join(' '));

const names = [];
const n = Math.floor(dynsym.size / dynsym.entsz);
for (let i = 0; i < n; i++) {
  const o = dynsym.off + i * dynsym.entsz;
  const nameOff = b.readUInt32LE(o);
  let s = ''; for (let k = dynstr.off + nameOff; b[k]; k++) s += String.fromCharCode(b[k]);
  names.push(s);
}
console.log('dynsym(' + n + '):', names.filter(Boolean).join(' '));

// 导出符号（有实际定义、非 UND）
const exported = [];
for (let i = 0; i < n; i++) {
  const o = dynsym.off + i * dynsym.entsz;
  const nameOff = b.readUInt32LE(o);
  const shndx = b.readUInt16LE(o + 6);
  const val = b.readBigUInt64LE(o + 8);
  let s = ''; for (let k = dynstr.off + nameOff; b[k]; k++) s += String.fromCharCode(b[k]);
  if (shndx !== 0 && s) exported.push(s + '@' + val.toString(16));
}
console.log('exported defs:', exported.join(' ') || '(none)');
