cmake ..
make -j 8

cp SquidStorageServer ../test/test_server/SquidStorageServer
cp DataNode ../test/test_datanode1/DataNode
cp DataNode ../test/test_datanode2/DataNode
cp DataNode ../test/test_datanode3/DataNode
cp SquidStorage ../test/test_client1/SquidStorage
cp SquidStorage ../test/test_client2/SquidStorage

gnome-terminal --tab --title="Server" -- bash -c "cd ../test/test_server && ./SquidStorageServer; exec bash"
gnome-terminal --tab --title="DataNode1" -- bash -c "cd ../test/test_datanode1 && ./DataNode; exec bash"
gnome-terminal --tab --title="DataNode2" -- bash -c "cd ../test/test_datanode2 && ./DataNode; exec bash"
#gnome-terminal --tab --title="DataNode3" -- bash -c "cd ../test/test_datanode3 && ./DataNode; exec bash"
gnome-terminal --tab --title="Client1" -- bash -c "cd ../test/test_client1 && ./SquidStorage; exec bash"
gnome-terminal --tab --title="Client2" -- bash -c "cd ../test/test_client2 && ./SquidStorage; exec bash"

rm -f ../test/test_client1/SquidStorage
rm -f ../test/test_client2/SquidStorage
rm -f ../test/test_server/SquidStorageServer
rm -f ../test/test_datanode1/DataNode
rm -f ../test/test_datanode2/DataNode
rm -f ../test/test_datanode3/DataNode
# make clean
