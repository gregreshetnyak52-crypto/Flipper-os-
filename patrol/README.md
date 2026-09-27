# Patrol — guard tour and inspection rounds for Flipper Zero

Patrol turns a Flipper Zero into a guard tour / inspection round recorder.
Stick cheap NFC tags, 125 kHz RFID tags or iButton keys along a route; the
guard (or technician, cleaner, farmer…) taps them with the Flipper during each
round. Every event goes into a **tamper-evident log**: each record is signed
with HMAC-SHA-256 over the previous signature, so editing, deleting,
reordering or inventing records is detected by the offline report page.

- **Three tag technologies** at once: NFC (any ISO 14443-3A: NTAG, MIFARE…),
  125 kHz RFID (EM4100, HID Prox and the other formats the Flipper reads) and
  iButton (Dallas, Cyfral, Metakom). Only the tag's ID is read; tags are
  never written, so existing badges and key fobs can be reused.
- **Route awareness**: shows the next checkpoint, flags checkpoints scanned
  out of order, unknown tags and missed checkpoints, and switches the Flipper
  to the right radio for the next checkpoint after each scan.
- **Issue reporting**: press Up at a checkpoint to log a problem there.
- **Receipt codes**: at the end of a round the Flipper shows a short code
  (e.g. `3fa9-c21b`) tied to the signed log. The guard sends it to the
  supervisor right away; the supervisor finds it in the verified report.
- **Secrets stay on the device**: the site key and the admin PIN are
  encrypted on the SD card with the Flipper's secure-enclave device key, the
  same way the built-in U2F app protects its keys. A copied `config.txt` is
  useless on another Flipper.
- **Admin PIN** (four arrow presses) protects the route, the site key and
  key rotation. Failed PIN attempts are logged.
- **Offline report**: [`companion/patrol-report.html`](../companion/patrol-report.html)
  verifies the logs and summarises rounds (completion, missed checkpoints,
  issues, unknown tags, out-of-order scans, receipts), exports CSV and prints
  to PDF. It runs entirely in the browser; nothing is uploaded.

## Using it

**Supervisor, once per site**

1. Open *Patrol → Admin*. On first use there is no PIN yet: choose
   *Set admin PIN* and enter four arrow presses twice.
2. *Add checkpoints*: pick the tag type with Left/Right, hold each tag to the
   back of the Flipper in the order the route should be walked, and name it
   (`_` types a space). The order of enrolment is the route order.
3. *Show site key*: copy the 64 hex digits somewhere safe (password manager).
   Anyone with the key could produce valid-looking records, so keep it away
   from guards.

**Guard, every round**

1. *Start round*. The screen shows the round number, progress, elapsed time
   and the next checkpoint with its tag type.
2. Tap each checkpoint. `OK` = in order, `??` = out of order / already
   scanned, inverted `!!` = unknown tag. The radio in the bottom-left corner
   follows the route automatically; Left/Right changes it by hand (it is
   shown inverted when it does not match the next checkpoint).
3. Up reports an issue at the last scanned checkpoint.
4. Hold OK (or press Back) and confirm to end the round. Send the receipt
   code to the supervisor.

**Supervisor, reviewing**

Copy `SD:/apps_data/patrol/log.csv` (and any `log_N.csv`) to a computer, open
`patrol-report.html`, choose the files, paste the site key and press *Verify*.

## Files

All in `SD:/apps_data/patrol/`:

| File | Content |
|---|---|
| `log.csv` | The signed event log, one record per line (below) |
| `log_N.csv` | Previous logs, kept when a new site key was created |
| `checkpoints.txt` | The route: `TECH;UID;Name` per line, in order |
| `config.txt` | Encrypted site key and PIN, round counter |

Log columns: `seq,time,event,round,checkpoint,name,tech,uid,detail,mac`.
Events: `ROUND_START`, `SCAN`, `UNKNOWN`, `ISSUE`, `ROUND_END`,
`CHECKPOINT_ADDED`, `CHECKPOINT_DELETED`, `ADMIN_PIN_SET`,
`ADMIN_PIN_FAILED`, `KEY_CREATED`.

`mac` = first 16 bytes (hex) of `HMAC-SHA-256(site key, previous mac + "\n" +
the line without its mac)`; the first record uses `GENESIS` as the previous
mac. The receipt code is the first 8 hex digits of the `ROUND_END` record's
mac.

## What it does and does not prove

- ✔ Records were written by a Patrol app holding the site key, in that order,
  and none were changed, removed or inserted after the fact.
- ✔ A tag with that ID was in front of the Flipper when the record was made.
- ✘ Cutting records off the **end** of the file cannot be seen from the file
  alone. Compare the last record number (shown with every receipt) with the
  report.
- ✘ Times come from the Flipper's clock, which the user can change in
  Settings. The report flags any point where time runs backwards.
- ✘ Plain NFC/RFID IDs can be cloned: someone could copy a checkpoint tag and
  scan the copy elsewhere. For high-assurance sites use tags that are hard to
  reach or mount them tamper-evidently; cryptographic tags (NTAG 424 DNA) are
  on the roadmap.

## Building

```sh
cd patrol
ufbt            # → dist/patrol.fap
ufbt launch     # build, upload and start over USB
```

It is also included in the FlipperOS firmware build (`firmware/build.sh`).
Host tests (HMAC against Python, report verifier against logs signed by the
app's C code): `tests/run_patrol_tests.sh`.
