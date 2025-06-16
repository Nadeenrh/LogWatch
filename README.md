# LogWatch

🛡️ A real-time Linux directory and file activity logger built using `inotify`.

Tracks:
- File/directory creation, deletion, modification, and access
- Recursively monitors subdirectories
- Throttles repetitive `IN_ACCESS` logs (e.g., from editors/typing)
- Logs to `logwatch.log` and prints to terminal

## Usage

```bash
./logwatch [directory]

