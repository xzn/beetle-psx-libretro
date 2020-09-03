#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

struct OneOf;
struct AllOf;
struct OneOfVal;

using OneOfPtr = std::shared_ptr<OneOf>;
using AllOfPtr = std::shared_ptr<AllOf>;
using OneOfValPtr = std::shared_ptr<OneOfVal>;

using Define = std::variant<std::string_view, OneOfPtr, AllOfPtr, OneOfValPtr>;
using Val = std::variant<int, float, std::string_view>;
using Graphic = std::pair<std::string_view, std::string_view>;
using Compute = std::string_view;

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
struct OneOfVal
{
	std::string_view name;
	std::vector<Val> vals;
	operator Define()
	{
		return OneOfValPtr{ new OneOfVal{ *this } };
	}
};

struct ShaderList
{
	std::vector<Program> programs;
};

extern const ShaderList shader_list;
