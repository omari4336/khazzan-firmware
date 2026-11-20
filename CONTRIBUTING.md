# Contributing to Khazzan

Team members should accept their GitHub collaborator invitations before pushing branches.

## Pick and implement a task

1. Create or claim a GitHub issue so teammates can see what you are working on.
2. Pull the latest main branch and create a branch named for your change, such as feature/rtc-logging or fix/sensor-timeout.
3. Keep each pull request focused on one change. Push your branch and open a pull request targeting main.

## Review and merge

- Link the issue and describe what changed and why.
- For firmware changes, build the affected PlatformIO environment and list the build command in the pull request.
- If you tested on hardware, record the board, setup, and observed results. State which checks still need hardware if you only built locally.
- Ask a teammate to review before merging. Use the pull request to resolve feedback, then squash merge and remove the merged branch.

Keep local serial port settings, secrets, and raw device logs out of commits. Include small redacted log excerpts in the pull request when they help explain test results. Update the wiring documentation if pin assignments or connections change.