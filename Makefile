all:
	$(MAKE) -C src
	$(MAKE) -C simulations

clean:
	$(MAKE) -C src clean
	$(MAKE) -C simulations clean
