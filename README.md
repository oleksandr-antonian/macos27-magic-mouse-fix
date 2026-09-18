# magicmousefix

Your Magic Mouse pairs, says *Connected*, shows its battery — and does nothing at all.
That's macOS 27. This fixes it.

## Why it happens

macOS 27 forgets to send the mouse one four-byte message that switches it into multi-touch
mode. The mouse sits there waiting for a command that never comes. macOS 26 sent it; so does
the Linux `hid-magicmouse` driver, which is where the bytes come from:

```
F1 06 01 37
```

This program sends that message. That's all it does.

Tested on a Mac mini M4, macOS 27.0, Magic Mouse 2 (`0x0269`, firmware `1.9.2`) — the setup
most people are reporting. Firmware `3.1.4` is affected too.

## Fix it right now

```sh
clang -O2 -Wall -o magicmousefix magicmousefix.c -framework IOKit -framework CoreFoundation
sudo ./magicmousefix
```

The mouse should wake up immediately. It stays fixed until you disconnect it or the Mac
sleeps.

## Fix it for good

The agent re-sends the message whenever the mouse reconnects:

```sh
sudo mkdir -p /usr/local/bin
sudo cp magicmousefix /usr/local/bin/
sudo chmod 755 /usr/local/bin/magicmousefix
sudo cp com.local.magicmousefix.plist /Library/LaunchDaemons/
sudo chown root:wheel /Library/LaunchDaemons/com.local.magicmousefix.plist
sudo chmod 644 /Library/LaunchDaemons/com.local.magicmousefix.plist
sudo launchctl bootstrap system /Library/LaunchDaemons/com.local.magicmousefix.plist
```

Build the binary first — the repository ships source only, so `magicmousefix` has to exist in
the folder you copy from.

`Bootstrap failed: 5: Input/output error` almost always means the daemon is already loaded.
Check with `sudo launchctl print system/com.local.magicmousefix` — if it says `state = running`,
you are done. If you are reinstalling, `bootout` first:

```sh
sudo launchctl bootout system/com.local.magicmousefix
```

Log lives in `/var/log/magicmousefix.log`. Toggle the mouse off and on — you should see a
line appear.

Still dead after sleep? Add `--heartbeat` and `60` to the arguments in the plist, then
`bootout` and `bootstrap` again.

## Undo it

```sh
sudo launchctl bootout system/com.local.magicmousefix
sudo rm /Library/LaunchDaemons/com.local.magicmousefix.plist /usr/local/bin/magicmousefix
```

## Before you run a stranger's code

Fair question. It's one file, about 180 lines, and it does nothing clever: no network, no
reading your input, no kernel extension, no touching the system volume. Read it first —
that's why it ships as source and not a binary.

Running as root avoids needing Input Monitoring permission. Without `sudo`, grant Terminal
that permission instead. The mouse exposes four HID interfaces and one of them often refuses
the report no matter who is asking — three getting through is enough.

## Please tell Apple too

This is a workaround, not a fix. Report it at
[Apple Feedback](https://www.apple.com/feedback/macos.html) with your firmware version:

```sh
system_profiler SPBluetoothDataType | grep -A 20 "Magic Mouse"
```

## Credit

The `F1 06 01 37` trick was first posted in
[o0mohd0o/MagicMouseFix](https://github.com/o0mohd0o/MagicMouseFix); the symptoms were
pieced together in [this Apple thread](https://discussions.apple.com/thread/256357286).
This is a separate implementation, written to be read in one sitting.

MIT licensed.
