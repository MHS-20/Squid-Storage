cmake ..
make -j 8

mv SquidStorage ../test_txt/test_client/SquidStorage
mv SquidStorageServer ../test_txt/test_server/SquidStorageServer
mv DataNode ../test_txt/test_datanode/DataNode

gnome-terminal --tab --title="Server" -- bash -c "cd ../test_txt/test_server && ./SquidStorageServer; exec bash"
gnome-terminal --tab --title="DataNode" -- bash -c "cd ../test_txt/test_datanode && ./DataNode; exec bash"
gnome-terminal --tab --title="Client" -- bash -c "cd ../test_txt/test_client && ./SquidStorage; exec bash"

rm -rf ../test_txt/test_client/SquidStorage
rm -rf ../test_txt/test_server/SquidStorageServer
rm -rf ../test_txt/test_datanode/DataNode
make clean
