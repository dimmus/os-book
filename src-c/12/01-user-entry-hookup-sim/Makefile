CXX := clang++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -pedantic
TARGET := user_entry_hookup_sim

all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)

