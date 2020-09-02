struct ShaderList
{
    struct Program
    {

    };
};

namespace {

struct Program {};
struct OneOf {};
struct AllOf {};
struct Shaders {
    operator ShaderList()
    {
        return {};
    }
};

template <typename... Ts>
Program prog(Ts... as) { return {}; }

template <typename... Ts>
OneOf one_of(Ts... as) { return {}; }

template <typename... Ts>
AllOf all_of(Ts... as) { return {}; }

template <typename... Ts>
Shaders shaders(Ts... as) { return {}; }

}
