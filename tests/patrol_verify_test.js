// Runs the verifier embedded in companion/patrol-report.html against a log
// produced by tests/patrol_make_log.c (same MAC code as the Flipper app).
"use strict";
const fs = require("fs");
const path = require("path");
const assert = require("assert");

const html = fs.readFileSync(path.join(__dirname, "../companion/patrol-report.html"), "utf8");
const core = /<script id="patrol-core">([\s\S]*?)<\/script>/.exec(html)[1];
const PatrolCore = new Function("module", core + "\nreturn PatrolCore;")({});

const log = fs.readFileSync(process.argv[2], "utf8");
const expectedReceipt = process.argv[3];
const key = PatrolCore.parseKey(Array.from({ length: 32 }, (_, i) => (0xa0 + i).toString(16)).join(""));

// Genuine log
let records = PatrolCore.parseLog(log, "log.csv");
let summary = PatrolCore.verify(records, key);
assert.strictEqual(summary.broken, null, "genuine log must verify");
assert.strictEqual(summary.clockJumps.length, 0);
assert.strictEqual(summary.total, 14);
const rounds = PatrolCore.buildRounds(records);
assert.strictEqual(rounds.length, 2);
assert.deepStrictEqual(rounds[0].missed, ["Boiler room"]);
assert.strictEqual(rounds[0].issues, 1);
assert.strictEqual(rounds[0].unknown, 1);
assert.strictEqual(rounds[0].outOfOrder, 1);
assert.strictEqual(rounds[0].device, "Guard1");
assert.strictEqual(rounds[1].missed.length, 0);
assert.strictEqual(rounds[1].receipt, expectedReceipt, "receipt must match the device");
console.log("ok  genuine log verifies, rounds and receipt match");

// Wrong key
summary = PatrolCore.verify(PatrolCore.parseLog(log), PatrolCore.parseKey("00".repeat(32)));
assert.strictEqual(summary.broken.seq, 1);
console.log("ok  wrong key rejected at record 1");

// Edited record: turn the missed checkpoint into a visited one
const lines = log.split("\n");
const edited = lines.map((l) => l.replace("visited=2/3", "visited=3/3")).join("\n");
summary = PatrolCore.verify(PatrolCore.parseLog(edited), key);
assert.strictEqual(summary.broken.seq, 9);
assert.strictEqual(summary.broken.reason, "signature mismatch");
console.log("ok  edited record detected at #9");

// Deleted record: drop the UNKNOWN tag line
const deleted = lines.filter((l) => !l.includes(",UNKNOWN,")).join("\n");
summary = PatrolCore.verify(PatrolCore.parseLog(deleted), key);
assert.strictEqual(summary.broken.seq, 7);
console.log("ok  deleted record detected at #7");

// Forged record inserted with a made-up MAC
const forged = lines.slice(0, 7).concat(
  ["7,2026-09-27 22:04:00,SCAN,1,2,Boiler room,RFID,01020304AB,in order,0123456789abcdef0123456789abcdef"],
  lines.slice(7)).join("\n");
summary = PatrolCore.verify(PatrolCore.parseLog(forged), key);
assert.strictEqual(summary.broken.seq, 7);
console.log("ok  forged record detected at #7");

// Swapped records
const swapped = lines.slice();
[swapped[5], swapped[6]] = [swapped[6], swapped[5]];
summary = PatrolCore.verify(PatrolCore.parseLog(swapped.join("\n")), key);
assert.ok(summary.broken);
console.log("ok  reordered records detected");

// Clock set back on the device: records stay authentic but get flagged
{
  const { execFileSync } = require("child_process");
  const back = execFileSync(process.argv[4], ["clock-back"]).toString();
  summary = PatrolCore.verify(PatrolCore.parseLog(back), key);
  assert.strictEqual(summary.broken, null);
  assert.strictEqual(summary.clockJumps.length, 1);
  assert.strictEqual(summary.clockJumps[0].seq, 3);
  console.log("ok  clock set backwards is flagged");
}
