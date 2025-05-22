#!/bin/bash
# Move executables to their respective directories
cp ./SquidStorage ./test_client1/SquidStorage
cp ./SquidStorage ./test_client2/SquidStorage
cp ./SquidStorageServer ./test_server/SquidStorageServer
cp ./DataNode ./test_datanode1/DataNode
cp ./DataNode ./test_datanode2/DataNode
cp ./DataNode ./test_datanode3/DataNode

# Start a new tmux session
SESSION_NAME="SquidStorage"
tmux new-session -d -s $SESSION_NAME
tmux set -g mouse on

# Run SquidStorageServer in the first pane
tmux rename-window -t $SESSION_NAME "Server"
tmux send-keys -t $SESSION_NAME "cd ./test_server && ./SquidStorageServer" C-m

# Split the window and run DataNode in the second pane
tmux split-window -h
tmux send-keys "cd ./test_datanode1 && ./DataNode" C-m

# Split the window and run DataNode in the second pane
tmux split-window -h
tmux send-keys "cd ./test_datanode2 && ./DataNode" C-m

# Split the window and run DataNode in the second pane
tmux split-window -h
tmux send-keys "cd ./test_datanode3 && ./DataNode" C-m

# Split the window vertically and run SquidStorage in the third pane
tmux split-window -v
tmux send-keys "cd ./test_client1 && ./SquidStorage" C-m

tmux split-window -v
tmux send-keys "cd ./test_client2 && ./SquidStorage" C-m

# Attach to the tmux session
tmux select-pane -t 0
tmux attach-session -t $SESSION_NAME

# Clean up executables after execution
rm -f ./test_client1/SquidStorage
rm -f ./test_client2/SquidStorage
rm -f ./test_server/SquidStorageServer
rm -f ./test_datanode1/DataNode
rm -f ./test_datanode2/DataNode
rm -f ./test_datanode3/DataNode
