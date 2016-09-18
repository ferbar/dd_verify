
all: dd_verify
dd_verify: main.cpp fmt_no.c
	g++ -g main.cpp fmt_no.c -o dd_verify

install: all
	cp dd_verify ~/bin
