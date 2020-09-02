#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

struct ShaderList
{
	struct OneOf;
	struct AllOf;

	using OneOfPtr = std::shared_ptr<OneOf>;
	using AllOfPtr = std::shared_ptr<AllOf>;

	using Define = std::variant<std::string, OneOfPtr, AllOfPtr>;

	struct Program
	{
		std::variant<std::pair<std::string, std::string>, std::string> files;
		std::vector<Define> defines;
	};
	struct OneOf
	{
		std::vector<Define> defines;
	};
	struct AllOf
	{
		std::vector<Define> defines;
	};

	std::vector<Program> programs;
};

extern const ShaderList shader_list;

namespace
{

using namespace std;
using Shaders = ShaderList;
using Program = ShaderList::Program;
using OneOf = ShaderList::OneOf;
using AllOf = ShaderList::AllOf;
using OneOfPtr = ShaderList::OneOfPtr;
using AllOfPtr = ShaderList::AllOfPtr;
using Define = ShaderList::Define;

Shaders shaders_sub(Shaders s)
{
	return s;
}
template <typename... Ts>
Shaders shaders_sub(Shaders s, Program p, Ts... as)
{
	s = shaders_sub(s, move(as)...);
	s.programs.push_back(move(p));
	return s;
}
template <typename... Ts>
Shaders shaders(Ts... as)
{
	auto s = Shaders{};
	return shaders_sub(move(s), move(as)...);
}

Program prog_sub(Program p)
{
	return p;
}
template <typename... Ts>
Program prog_sub(Program p, Define v, Ts... as)
{
	p = prog_sub(move(p), as...);
	p.defines.push_back(v);
	return p;
}
template <typename... Ts>
Program prog(string v, string f, Ts... as)
{
	auto p = Program{ pair<string, string>{ move(v), move(f) } };
	return prog_sub(move(p), as...);
}
template <typename... Ts>
Program prog(string c, Ts... as)
{
	auto p = Program{ string{ move(c) } };
	return prog_sub(move(p), as...);
}

OneOfPtr one_of()
{
	return OneOfPtr{ new OneOf };
}
template <typename... Ts>
OneOfPtr one_of(OneOfPtr v, Ts... as);
template <typename... Ts>
OneOfPtr one_of(AllOfPtr v, Ts... as);
template <typename... Ts>
OneOfPtr one_of(string v, Ts... as)
{
	auto vs = one_of(as...);
	vs->defines.push_back(v);
	return vs;
}
template <typename... Ts>
OneOfPtr one_of(OneOfPtr v, Ts... as)
{
	auto vs = one_of(as...);
	vs->defines.push_back(v);
	return vs;
}
template <typename... Ts>
OneOfPtr one_of(AllOfPtr v, Ts... as)
{
	auto vs = one_of(as...);
	vs->defines.push_back(v);
	return vs;
}

AllOfPtr all_of()
{
	return AllOfPtr{ new AllOf };
}
template <typename... Ts>
AllOfPtr all_of(OneOfPtr v, Ts... as);
template <typename... Ts>
AllOfPtr all_of(AllOfPtr v, Ts... as);
template <typename... Ts>
AllOfPtr all_of(string v, Ts... as)
{
	auto vs = all_of(as...);
	vs->defines.push_back(v);
	return vs;
}
template <typename... Ts>
AllOfPtr all_of(OneOfPtr v, Ts... as)
{
	auto vs = all_of(as...);
	vs->defines.push_back(v);
	return vs;
}
template <typename... Ts>
AllOfPtr all_of(AllOfPtr v, Ts... as)
{
	auto vs = all_of(as...);
	vs->defines.push_back(v);
	return vs;
}

} // namespace
