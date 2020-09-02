struct ShaderList
{
    struct Program
    {

    };
};

namespace {

struct Program {};
struct Define {};
struct Option {};
struct Shaders {
    operator ShaderList()
    {
        return {};
    }
};

template <typename... Ts>
Define def(Ts... as) { return {}; }

template <typename... Ts>
Program prog(Ts... as) { return {}; }

template <typename... Ts>
Option opt(Ts... as) { return {}; }

template <typename... Ts>
Shaders shaders(Ts... as) { return {}; }

}
