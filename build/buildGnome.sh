cmake ..
make -j 8

mv SquidStorageServer ../test_txt/test_server/SquidStorageServer
mv DataNode ../test_txt/test_datanode/DataNode
cp SquidStorage ../test_txt/test_client1/SquidStorage
mv SquidStorage ../test_txt/test_client2/SquidStorage

gnome-terminal --tab --title="Server" -- bash -c "cd ../test_txt/test_server && ./SquidStorageServer; exec bash"
gnome-terminal --tab --title="DataNode" -- bash -c "cd ../test_txt/test_datanode && ./DataNode; exec bash"
gnome-terminal --tab --title="Client" -- bash -c "cd ../test_txt/test_client1 && ./SquidStorage; exec bash"
gnome-terminal --tab --title="Client" -- bash -c "cd ../test_txt/test_client2 && ./SquidStorage; exec bash"

rm -rf ../test_txt/test_client1/SquidStorage
rm -rf ../test_txt/test_client2/SquidStorage
rm -rf ../test_txt/test_server/SquidStorageServer
rm -rf ../test_txt/test_datanode/DataNode
make clean
