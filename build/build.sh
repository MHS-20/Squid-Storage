#!/bin/bash

# Build the project
cmake ..
make -j 8

# Move executables to their respective directories
pwd
mv ./SquidStorage ../test_txt/test_client/SquidStorage
mv ./SquidStorageServer ../test_txt/test_server/SquidStorageServer
mv ./DataNode ../test_txt/test_datanode/DataNode

# Start a new tmux session
SESSION_NAME="SquidStorage"
tmux new-session -d -s $SESSION_NAME

# Run SquidStorageServer in the first pane
tmux rename-window -t $SESSION_NAME "Server"
tmux send-keys -t $SESSION_NAME "cd ../test_txt/test_server && SquidStorageServer" C-m

# Split the window and run DataNode in the second pane
tmux split-window -h
tmux send-keys "cd ../test_txt/test_datanode && ./DataNode" C-m

# Split the window vertically and run SquidStorage in the third pane
tmux split-window -v
tmux send-keys "cd ../test_txt/test_client && ./SquidStorage" C-m

# Attach to the tmux session
tmux select-pane -t 0
tmux attach-session -t $SESSION_NAME

# Clean up executables after execution
rm -rf ../test_txt/test_client/SquidStorage
rm -rf ../test_txt/test_server/SquidStorageServer
rm -rf ../test_txt/test_datanode/DataNode
make clean