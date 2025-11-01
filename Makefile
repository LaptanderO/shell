all: build run

build: kubsh.c vfs.c
	gcc vfs.c kubsh.c -l readline -l fuse3 -o kubsh

run: kubsh
	sudo ./kubsh

clean:
	rm kubsh
