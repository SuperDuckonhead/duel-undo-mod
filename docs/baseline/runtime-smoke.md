# Isolated baseline runtime smoke

Prepare a new ignored test directory using:

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Prepare-SmokeRuntime.ps1 -RuntimeRoot F:/MyCardLibrary/ygopro -OutputName baseline-runtime
~~~

The script refuses an existing destination. It links installed read-only resource inputs directly (directory junctions and file hard links), while configuration, editable deck files, logs and replay output live in the test directory. It does not copy the original system.conf or private deck library. Do not edit or recursively delete linked resource directories; they refer to the original installation. Tests create only controlled decks under the stage's own deck directory.

Observed on 2026-09-10: the original installed ygopro.exe was launched with this directory as its working directory. After four seconds it was alive with title "YGOPro FPS: 60" and working set 157880320 bytes. The test-owned process was then terminated. The original system.conf SHA-256 was unchanged. This is an initialization/render-loop smoke, not visual verification, editor-save testing, a completed duel, or graceful-exit coverage.

The newly built client will use the same resource staging for comparison. Its compatibility result is still pending. Desktop automation failed at kernel initialization, before any app interaction.
