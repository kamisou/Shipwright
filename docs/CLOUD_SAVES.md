# Cloud saves with Google Drive

Ship of Harkinian can synchronize its three save slots through a folder mirrored by Google Drive for desktop. The
game does not request or store Google credentials; Google Drive remains responsible for authentication and transport.

## Setup

1. Install Google Drive for desktop and sign in on every device.
2. In Ship of Harkinian, open **Settings > Cloud Saves**.
3. Enter the path to **My Drive** (or another folder that Google Drive mirrors).
4. Enable **Cloud saves** and **Sync automatically**, then choose **Sync now**.
5. Repeat with the same Drive folder on the other devices.

The synchronized files are stored under `Ship of Harkinian/Save` inside the selected folder. Only `global.sav` and
`file1.sav` through `file3.sav` are synchronized. Copying or deleting a slot is reflected in Drive when automatic sync
is enabled.

## Conflicts and recovery

At startup, the newest differing copy wins. Before replacing the older copy, the game preserves it beside the original
with `-conflict-<timestamp>.sav.bak` in its name. Explicit **Upload local saves** and **Download Drive saves** actions
also back up any differing destination first.

Do not play on two devices at the same time. Google Drive may not have delivered the other device's latest file before
the next save is uploaded. If synchronization fails, local saves remain available and the status shown in the Cloud
Saves screen describes the error.
