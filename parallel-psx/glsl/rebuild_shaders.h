#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

struct OneOf;
struct AllOf;

using OneOfPtr = std::shared_ptr<OneOf>;
using AllOfPtr = std::shared_ptr<AllOf>;

using Define = std::variant<std::string, OneOfPtr, AllOfPtr>;

using Graphic = std::pair<std::string, std::string>;
using Compute = std::string;

struct Program
{
	std::variant<Graphic, Compute> files;
	Define defines;
};
struct OneOf
{
	std::vector<Define> defines;
	operator Define()
	{
		return OneOfPtr{ new OneOf{ *this } };
	}
};
struct AllOf
{
	std::vector<Define> defines;
	operator Define()
	{
		return AllOfPtr{ new AllOf{ *this } };
	}
};

struct ShaderList
{
	std::vector<Program> programs;
};

extern const ShaderList shader_list;
