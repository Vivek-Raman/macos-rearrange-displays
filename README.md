# rearrange-displays

Apply named display layouts from an Apple Shortcut. It uses macOS's Core
Graphics framework directly. No Homebrew or third-party utility is needed.

## Setup

1. Capture your current display layout with a name:

   ```sh
   ./rearrange-displays.sh --capture left
   ```

2. The command writes the result to `config/layout-left.conf`. Make another
   arrangement in System Settings, then capture it:

   ```sh
   ./rearrange-displays.sh --capture center
   ```

   This writes `config/layout-center.conf`. Add as many named layouts as you
   need. Names may contain letters, numbers, hyphens, and underscores. Layout
   data stays out of the source code. The display at `(0, 0)` is the primary
   display.

   Add `--dry-run` to print a captured layout without changing its config file:

   ```sh
   ./rearrange-displays.sh --capture left --dry-run
   ```

   `--list` prints the names of saved layouts.

3. Create the Shortcut:

   1. Open **Shortcuts** and click **+** to create a new shortcut. Name it
      `Rearrange Displays`.
   2. Search for **Run Shell Script** and add that action.
   3. Replace the action's script with:

      ```sh
      ./rearrange-displays.sh --apply left
      ```

   You can assign a keyboard shortcut from the Shortcut Details panel if you
   want one. Create another shortcut with a different name after replacing
   `left` with another captured layout name.

The command requires a layout name when applying a layout. It fails instead of
choosing a layout when invoked without `--apply NAME`.

Here is my setup.

<img width="618" height="521" alt="image" src="https://github.com/user-attachments/assets/85ff4cc5-6d7f-42d8-9aad-ad0369dc2fce" />

## Testing

Run the script directly after configuring it:

```sh
./rearrange-displays.sh --apply left
```

If monitors are unplugged, a layout may fail. The script does not advance its
marker on failure, so the same layout is retried next time.
