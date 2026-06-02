Mock file tree for Docker runs.

Runtime files now live directly under the shared `SquidStorage/` root.
The containers all bind-mount the repository root there, so replicated files
will appear alongside the project files at the top level.

This lets you inspect the propagated files on the host after running the stack.
