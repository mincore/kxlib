
CXXFLAGS=-std=c++11 -Wno-format -fPIC -g -Wall -Iinclude

EXAMPLES=$(patsubst %.cpp,%,$(shell find examples -name "*.cpp"))

all: $(EXAMPLES)

clean:
	rm -f $(EXAMPLES) examples/*.o
