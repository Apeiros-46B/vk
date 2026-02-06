#include <fstream>
#include <ios>
#include <string>
#include <vector>

#include "sugar.hpp"

using namespace std;

auto read_file(string name) -> vector<char> {
	auto f = ifstream(name, ios::ate | ios::binary);
	if (!f.is_open()) throw std::runtime_error("failed to open file!");

	usz sz = f.tellg();
	auto buf = std::vector<char>(sz);
	f.seekg(0);
	f.read(buf.data(), sz);

	return buf;
}
