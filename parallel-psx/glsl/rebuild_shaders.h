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

using FileName = std::string_view;
using DefineName = std::string_view;
using Define = std::variant<DefineName, OneOfPtr, AllOfPtr, OneOfValPtr>;
using Val = int;

struct Program
{
	std::vector<FileName> files;
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
	DefineName name;
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
