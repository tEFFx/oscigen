CC=g++
CFLAGS=-c -Wall -std=c++11
LDFLAGS=-lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio
SOURCES=main.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=oscigen

all: $(SOURCES) $(EXECUTABLE)
    
$(EXECUTABLE): $(OBJECTS) 
	$(CC) obj/$(OBJECTS) -o bin/$@ $(LDFLAGS)

.cpp.o:
	$(CC) $(CFLAGS) $< -o obj/$@

clean:
	rm -rf bin obj
	mkdir bin obj
	rm -f *.o $(EXECUTABLE)
