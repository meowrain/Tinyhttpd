all: httpd
httpd: httpd.c
	gcc -g -W -Wall $(LIBS) -o $@ $<

clean:
	rm httpd
.PHONY: clean