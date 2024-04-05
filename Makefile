all: clean imsd

imsd:
	@${CC} ${LDFLAGS} -Wall -O2 imsd.c -o imsd -lpthread

	@chmod +x imsd

clean:
	@rm -rf imsd
