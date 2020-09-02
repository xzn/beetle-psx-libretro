#include "rebuild_shaders.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>

namespace fs = std::filesystem;

std::optional<std::string> read_file_to_string(std::string path)
{
	std::ifstream f(path, std::ios::ate | std::ios::binary);
	if (!f)
		return {};
	auto size = f.tellg();
	std::string ret(size, 0);
	f.seekg(0);
	f.read(ret.data(), size);
	size = f.gcount();
	ret.resize(size);
	return ret;
}

std::map<std::string, std::string> contents_of_files;

// Taken from cppreference.com
template <class... Ts>
struct overloaded : Ts...
{
	using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

int generate_program(const ShaderList::Program &p)
{
	std::visit(overloaded{
	               [](std::pair<std::string, std::string> f) {},
	               [](std::string f) {},
	           },
	           p.files);
	return 0;
}

int main()
{
	for (auto &p : shader_list.programs)
	{
		int ret = generate_program(p);
		if (ret)
			return ret;
	}
	return 0;
}
