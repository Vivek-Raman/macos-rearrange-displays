# rearrange-displays

Cycle between two display layouts from an Apple Shortcut. It uses macOS's Core
Graphics framework directly. No Homebrew or third-party utility is needed.

## Setup

1. Capture your current display layout:

   ```sh
   /Users/vivek/Documents/Code/ccbox/rearrange-displays/rearrange-displays.sh --capture A
   ```

2. The command writes the result to `config/layout-a.conf`. Make a second
   arrangement in System Settings, then capture it:

   ```sh
   /Users/vivek/Documents/Code/ccbox/rearrange-displays/rearrange-displays.sh --capture B
   ```

   This writes `config/layout-b.conf`. Layout data stays out of the source code.
   The display at `(0, 0)` is the primary display.

   Add `--dry-run` to print a captured layout without changing its config file:

   ```sh
   /Users/vivek/Documents/Code/ccbox/rearrange-displays/rearrange-displays.sh --capture A --dry-run
   ```

   `--list` is also available when you only want to inspect the connected
   display UUIDs and current positions.

3. In Shortcuts, add **Run Shell Script** and use:

   ```sh
   /Users/vivek/Documents/Code/ccbox/rearrange-displays/rearrange-displays.sh
   ```

The first invocation compiles the source, then applies layout A. Later
invocations alternate A and B. The current layout marker is stored at
`~/Library/Application Support/rearrange-displays/current-layout`.

## Testing

Run the script directly after configuring it:

```sh
/Users/vivek/Documents/Code/ccbox/rearrange-displays/rearrange-displays.sh
```

If monitors are unplugged, a layout may fail. The script does not advance its
marker on failure, so the same layout is retried next time.
