CC=g++
CFLAGS=-c -Wall `pkg-config fuse --cflags`
LDFLAGS=-lsqlite3 `pkg-config fuse --libs`
SOURCES=main.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=poormanfs

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

install:
	cp poormanfs /usr/local/bin/

build-deb:
	@equivs-build package.conf

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)
