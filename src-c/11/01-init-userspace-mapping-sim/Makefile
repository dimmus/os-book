CXX := clang++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -pedantic
TARGET := init_userspace_mapping_sim

all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)

