all: pbar

pbar: pbar.c
	gcc -o pbar pbar.c

clean:
	rm -f pipebar
