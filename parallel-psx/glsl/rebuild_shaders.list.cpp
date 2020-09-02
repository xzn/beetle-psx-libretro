#include "rebuild_shaders.h"

// ShaderList{{<Program>, *}}
// Program{Graphic{<std::string file.vert>, <std::string file.frag>}, <Define>}
// Program{Compute{<std::string file.comp>}, <Define>}
// OneOf{{<Define>, *}}
// AllOf{{<Define>, *}}
// <Define> = <std::string> | <OneOf> | <AllOf>

const ShaderList shader_list = ShaderList{{
    Program{
        Graphic{"primitive.vert", "primitive.frag"},
        OneOf{{
            "",
            AllOf{{
                "TEXTURED",
                OneOf{{
                    "OPAQUE",
                    "SEMI_TRANS_OPAQUE",
                    "SEMI_TRANS"
                }},
                OneOf{{
                    "",
                    "FILTER_XBR",
                    "FILTER_BILINEAR",
                    "FILTER_3POINT",
                    "FILTER_JINC2",
                    "FILTER_SABR"
                }}
            }}
        }}
    }
}};
