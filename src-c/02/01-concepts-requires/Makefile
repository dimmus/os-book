CXX := clang++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -pedantic
TARGET := concepts_requires

all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)

