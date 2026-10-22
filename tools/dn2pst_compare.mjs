// SPDX-License-Identifier: GPL-3.0-only
// FIZZ P1.5x: the C++ Digitone II reader (plugin/Dnsnd.cpp parseDn2, through tests/dnsnd_test --dn2) against
// FMToneSeq's JS (tools/dn2pst.mjs parseDn2pst + dn2pstToQuad): the 96-byte blob byte for byte, FIZZ's name, DRIVE,
// PAN, the three sends, FIZZ's tags, or the same refusal (its message too).
//
//   node tools/dn2pst_compare.mjs DN2PST_JS DNSND_TEST SCRATCH_DIR [REAL_DIR]
//
// The files: synthetic ones written into SCRATCH_DIR/dn2 by the JS's own writer (dn2RecordFromParams, wrapDn2Record,
// dn2pstZip): the JS self-test's sound (LZ4 and raw), AHD, octaves, every filter machine, machines refused, tags, a
// Windows-1252 name, a bare payload / body / record, a bad CRC, and 60 records with every parameter byte random (a
// fixed seed); and REAL_DIR's .dn2pst (the owner's, read where they are, never copied).
import { mkdirSync, writeFileSync, readdirSync, readFileSync, rmSync, existsSync } from "node:fs";
import { join, resolve } from "node:path";
import { execFileSync } from "node:child_process";
import { pathToFileURL } from "node:url";

const [jsPath, exe, scratch, realDir] = process.argv.slice(2);
if (!jsPath || !exe || !scratch) {
  console.error("usage: node dn2pst_compare.mjs DN2PST_JS DNSND_TEST SCRATCH_DIR [REAL_DIR]");
  process.exit(2);
}
const J = await import(pathToFileURL(resolve(jsPath)).href);
const dir = join(scratch, "dn2");
if (existsSync(dir)) rmSync(dir, { recursive: true });
mkdirSync(dir, { recursive: true });

// ---- the synthetic files
const want = {
  "lfo1.speed": 23.25, "lfo2.speed": -12.5, "lfo3.speed": 48, "lfo1.mult": 7, "lfo2.mult": 3, "lfo3.mult": 12,
  "lfo1.fade": -10, "lfo2.fade": 20, "lfo3.fade": 0, "lfo1.dest": 74, "lfo2.dest": 1, "lfo3.dest": 2,
  "lfo1.wave": 4, "lfo2.wave": 1, "lfo3.wave": 6, "lfo1.phase": 32, "lfo2.phase": 0, "lfo3.phase": 127,
  "lfo1.mode": 1, "lfo2.mode": 4, "lfo3.mode": 0, "lfo1.depth": 30.5, "lfo2.depth": -24.75, "lfo3.depth": 63.75,
  algo: 5, ratioC: 2, ratioA: 5.5, ratioB: 3 * 19 + 7, harm: -12.5, dtun: 33.25, fdbk: 90, mix: -20, phrt: 2,
  "envA.atk": 1, "envA.dec": 60, "envA.end": 10, "envA.lev": 100, "envB.atk": 2, "envB.dec": 70, "envB.end": 20, "envB.lev": 90,
  "envA.delay": 5, "envA.trig": 1, "envA.reset": 0, "envB.delay": 6, "envB.trig": 0, "envB.reset": 1,
  ofsC: 32, ofsA: -16, ofsB1: 0, ofsB2: -64, keyA: 40, keyB1: 30, keyB2: 20,
  "filter.type": 0, "filter.freq": 80.5, "filter.reso": 20.25, "filter.env": -30, "filter.delay": 3, "filter.atk": 4,
  "filter.dec": 50, "filter.sus": 60, "filter.rel": 127, "filter.keytrack": 50, "filter.base": 10.75, "filter.width": 100,
  "filter.reset": 1, "amp.atk": 8, "amp.hold": 127, "amp.dec": 40, "amp.sus": 90, "amp.rel": 45.5,
  "send.chorus": 10, "send.delay": 20, "send.reverb": 30, "amp.pan": -15, "amp.vol": 100, "amp.mode": 1, "amp.reset": 1,
  "fx.overdrive": 12,
};
const T = (...names) => names.reduce((m, n) => m | (1 << J.DN2_TAGS.indexOf(n)), 0) >>> 0;
const man = (name, tags, extra = {}) => ({ FormatVersion: "1.0", ProductType: [], Payload: name, FileType: "Sound",
  FirmwareVersion: "1.10E", MetaInfo: { Tags: tags }, ...extra });
const tagNames = (bits) => J.DN2_TAGS.filter((_, t) => (bits >>> t) & 1);
const put = (file, bytes) => writeFileSync(join(dir, file), bytes);
const pst = (file, rec, { compress = true, name = "SOUND", manifest } = {}) =>
  put(file, J.dn2pstZip(J.wrapDn2Record(rec, { compress }), manifest || man(name, tagNames((rec[8] << 24 | rec[9] << 16 | rec[10] << 8 | rec[11]) >>> 0))));
const rec0 = (p = want, opts = {}) => J.dn2RecordFromParams(p, { name: "SYNTH BASS", tagBits: T("BASS", "DARK"), ...opts });

pst("syn-lz4.dn2pst", rec0(), { name: "SYNTH BASS" });
pst("syn-raw.dn2pst", rec0(), { name: "SYNTH BASS", compress: false });
pst("ahd-note.dn2pst", rec0({ ...want, "amp.mode": 0, "amp.hold": 127, "amp.dec": 70 }));
pst("ahd-timed.dn2pst", rec0({ ...want, "amp.mode": 0, "amp.hold": 32, "amp.dec": 50 }));
pst("ahd-inf.dn2pst", rec0({ ...want, "amp.mode": 0, "amp.hold": 10, "amp.dec": 127 }));
for (const o of [-24, -12, -7, 5, 12, 24]) pst(`octave${o}.dn2pst`, rec0({ ...want, octave: o }));
for (const [fm, types] of [[0, [0, 20, 33, 64, 95, 96, 127]], [1, [0]], [2, [0]], [3, [0]], [4, [0, 1, 2, 3, 7]], [5, [0]], [9, [0]]])
  for (const ty of types) { const r = rec0({ ...want, "filter.type": ty }); r[245] = fm; pst(`filter-${fm}-${ty}.dn2pst`, r); }
pst("fdec-inf.dn2pst", rec0({ ...want, "filter.dec": 127, "filter.reset": 0, "amp.reset": 0 }));
for (const m of [1, 2, 3, 4, 7]) { const r = rec0(); r[244] = m; pst(`machine-${m}.dn2pst`, r); }
for (const [n, bits] of [["chord-pad", T("CHORD", "PAD")], ["kick", T("KICK")], ["sfx", T("SOUND FX")],
  ["lead-hat", T("LEAD", "HI-HAT")], ["none", 0], ["all", 0xffffffff]]) pst(`tags-${n}.dn2pst`, rec0(want, { tagBits: bits }));
{   // a Windows-1252 name: S/Z caron, A ring, O slash, AE, the euro, a NBSP, lower case
  const r = rec0();
  r.fill(0, 12, 28);
  [0x8a, 0x8e, 0xc5, 0xd8, 0xc6, 0x80, 0xa0, 0x9a, 0xe9, 0xff, 0x41, 0x7a, 0x20, 0x42].forEach((c, i) => { r[12 + i] = c; });
  pst("name-1252.dn2pst", r);
  const e = rec0();
  e.fill(0, 12, 28);
  pst("name-empty.dn2pst", e);
  const sp = rec0();
  sp.fill(0x20, 12, 28);
  sp[20] = 0x51;
  pst("name-spaces.dn2pst", sp);
}
pst("tags-manifest-differ.dn2pst", rec0(), { manifest: man("SYNTH BASS", ["PAD"]) });
pst("filetype-preset.dn2pst", rec0(), { manifest: man("SYNTH BASS", ["BASS", "DARK"], { FileType: "Preset" }) });
pst("payload-missing.dn2pst", rec0(), { manifest: man("ELSEWHERE", ["BASS", "DARK"]) });
put("bare-payload.dn2pst", J.wrapDn2Record(rec0()));
put("bare-body.dn2pst", J.wrapDn2Record(rec0(), { compress: false }).subarray(31, 31 + 364));
put("bare-record.dn2pst", rec0());
{
  const pl = J.wrapDn2Record(rec0());
  pl[pl.length - 10] ^= 0xff;                                   // the CRC wrong: read anyway, warned
  put("bad-crc.dn2pst", J.dn2pstZip(pl, man("SYNTH BASS", ["BASS", "DARK"])));
  const v3 = rec0(want, { version: 3 });
  pst("version3.dn2pst", v3);
  put("dn1.dn2pst", Uint8Array.from([0x44, 0x4e, 0x31, 0x53, ...new Array(360).fill(0)]));
  put("junk.dn2pst", Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8]));
}
{   // 60 records with every parameter byte random (offsets 30..241, the octave), a fixed seed
  let s = 20261022;
  const rnd = () => ((s = (s * 1103515245 + 12345) >>> 0) >>> 16) & 255;
  for (let k = 0; k < 60; k++) {
    const r = rec0();
    for (let o = 30; o < 242; o++) r[o] = rnd();
    r[244] = 0;
    r[245] = rnd() % 7;
    r[251] = k % 3 ? 0 : (rnd() % 49) - 24;
    pst(`random-${String(k).padStart(2, "0")}.dn2pst`, r, { compress: k % 2 === 0 });
  }
}

// ---- both readers
const hex = (b) => Array.from(b, (x) => x.toString(16).padStart(2, "0")).join("");
const args = ["--dn2", dir];
if (realDir && existsSync(realDir)) args.push(realDir);
const cpp = new Map(execFileSync(exe, args, { encoding: "utf8", maxBuffer: 1 << 26 }).trim().split("\n").map((l) => {
  const f = l.split("\t");
  return [f[0], f];
}));
const files = readdirSync(dir).filter((f) => f.endsWith(".dn2pst")).map((f) => join(dir, f));
if (realDir && existsSync(realDir)) files.push(...readdirSync(realDir).filter((f) => f.endsWith(".dn2pst")).map((f) => join(realDir, f)));
let ok = 0, bad = 0, refused = 0, real = 0;
for (const path of files.sort()) {
  const f = path.split("/").pop();
  const c = cpp.get(f);
  let js;
  try {
    const p = await J.parseDn2pst(new Uint8Array(readFileSync(path)));
    const q = J.dn2pstToQuad(p);
    const tags = J.FIZZ_KINDS.reduce((m, k, i) => m | (p.fizzTags.includes(k) ? 1 << i : 0), 0);
    js = [f, q.name, String(q.drive), String(q.pan), String(q.sends.reverb), String(q.sends.delay), String(q.sends.chorus),
          hex(q.blob), String(tags)];
  } catch (e) {
    js = [f, "ERROR", e.message];
  }
  if (realDir && resolve(path).startsWith(resolve(realDir))) real++;
  if (!c) { bad++; console.log(`  FAIL ${f}: no line from the C++`); continue; }
  if (js[1] === "ERROR" || c[1] === "ERROR") {
    if (js[1] === c[1] && js[2] === c[2]) { refused++; continue; }
    bad++; console.log(`  FAIL ${f}: JS ${js[1]} ${js[2] || ""}\n         C++ ${c[1]} ${c[2] || ""}`); continue;
  }
  const diff = js.findIndex((v, i) => v !== c[i]);
  if (diff >= 0) {
    bad++;
    console.log(`  FAIL ${f}: field ${diff}: JS ${js[diff]}\n${" ".repeat(10 + f.length)}C++ ${c[diff]}`);
  } else ok++;
}
rmSync(dir, { recursive: true });
console.log(`dn2pst_compare: ${files.length} files (${real} real), ${ok} identical, ${refused} refused by both (the same ` +
            `message), ${bad} different`);
process.exit(bad || !files.length ? 1 : 0);
