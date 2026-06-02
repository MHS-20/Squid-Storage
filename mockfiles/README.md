Mock file tree for Docker runs.

The client container reads from `mockfiles/client`, while the server and
data nodes write into their own bind-mounted folders under `mockfiles/`.

This lets you inspect the propagated files on the host after running the stack.
