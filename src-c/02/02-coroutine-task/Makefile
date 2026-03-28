CXX := clang++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -pedantic
TARGET := coroutine_task

all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)

