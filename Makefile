all: build

build: kubsh

kubsh: kubsh.c vfs.c
	gcc kubsh.c vfs.c -lreadline -lfuse3 -o kubsh

run: kubsh
	./kubsh

deb: kubsh
	mkdir -p kubsh-package/DEBIAN
	mkdir -p kubsh-package/usr/bin
	cp kubsh kubsh-package/usr/bin/
	cp DEBIAN/control kubsh-package/DEBIAN/
	dpkg-deb --build kubsh-package kubsh_1.0_amd64.deb

clean:
	rm -f kubsh
	rm -rf kubsh-package
	rm -f *.deb
