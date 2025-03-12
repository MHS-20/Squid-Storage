.PHONY: all clean

all:
	$(MAKE) -C data-node
	$(MAKE) -C server

run:
	$(MAKE) -C server run &
	$(MAKE) -C data-node run

clean:
	$(MAKE) -C data-node clean
	$(MAKE) -C server clean