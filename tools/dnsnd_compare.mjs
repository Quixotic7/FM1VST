// SPDX-License-Identifier: GPL-3.0-only
// The C++ .dnsnd reader (plugin/Dnsnd.cpp, through tests/dnsnd_test) against ChoralRoot's JS (web/fm1dnsnd.js
// dnsndToQuad, fm1sounds.js soundFromDnsnd's name) on a folder of presets: the 96-byte blob byte for byte, the name,
// DRIVE, PAN, the three sends (the JS's params rounded as the C++ does), or the same refusal.
//
//   node tools/dnsnd_compare.mjs CHORALROOT_WEB_DIR PRESETS_DIR DNSND_TEST
import { readFileSync, readdirSync } from "node:fs";
import { join, resolve } from "node:path";
import { execFileSync } from "node:child_process";
import { pathToFileURL } from "node:url";

const [web, dir, exe] = process.argv.slice(2);
if (!web || !dir || !exe) { console.error("usage: node dnsnd_compare.mjs CHORALROOT_WEB_DIR PRESETS_DIR DNSND_TEST"); process.exit(2); }
const { parseDnsnd, dnsndToQuad } = await import(pathToFileURL(resolve(web, "fm1dnsnd.js")).href);
const { soundFromDnsnd } = await import(pathToFileURL(resolve(web, "fm1sounds.js")).href);
const cpp = new Map(execFileSync(exe, [dir], { encoding: "utf8", maxBuffer: 1 << 26 }).trim().split("\n").map((l) => {
  const f = l.split("\t");
  return [f[0], f];
}));
const files = readdirSync(dir).filter((f) => f.endsWith(".dnsnd")).sort();
const hex = (b) => Array.from(b, (x) => x.toString(16).padStart(2, "0")).join("");
const c7 = (v) => Math.max(0, Math.min(127, Math.round(v || 0)));
let ok = 0, bad = 0, refused = 0;
for (const f of files) {
  const c = cpp.get(f);
  let js;
  try {
    const p = await parseDnsnd(new Uint8Array(readFileSync(join(dir, f))));
    const q = dnsndToQuad(p);
    js = [f, soundFromDnsnd(p).name, String(q.drive), String(q.pan), String(c7(p.params["send.reverb"])),
          String(c7(p.params["send.delay"])), String(c7(p.params["send.chorus"])), hex(q.blob)];
  } catch (e) {
    js = [f, "ERROR"];
  }
  if (!c) { bad++; console.log(`  FAIL ${f}: no line from the C++`); continue; }
  if (js[1] === "ERROR" || c[1] === "ERROR") {
    if (js[1] === c[1]) { refused++; continue; }
    bad++; console.log(`  FAIL ${f}: JS ${js[1]} / C++ ${c[1]} ${c[2] || ""}`); continue;
  }
  const diff = js.findIndex((v, i) => v !== c[i]);
  if (diff >= 0) {
    bad++;
    console.log(`  FAIL ${f}: field ${diff}: JS ${js[diff]}\n${" ".repeat(10 + f.length)}C++ ${c[diff]}`);
  } else ok++;
}
console.log(`dnsnd_compare: ${files.length} presets, ${ok} identical, ${refused} refused by both, ${bad} different`);
process.exit(bad || !files.length ? 1 : 0);
