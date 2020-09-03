#include "rebuild_shaders.h"

// ShaderList{{<Program>, *}}
// Program{Graphic{<std::string file.vert>, <std::string file.frag>}, <Define>}
// Program{Compute{<std::string file.comp>}, <Define>}
// OneOf{{<Define>, *}}
// AllOf{{<Define>, *}}
// <Define> = <std::string> | <OneOf> | <AllOf>

const ShaderList shader_list = ShaderList{{
    Program{
        {"primitive.vert", "primitive.frag"},
        OneOf{{ "", AllOf{{
            "TEXTURED",
            OneOf{{ "OPAQUE", "SEMI_TRANS_OPAQUE", "SEMI_TRANS" }},
            OneOf{{
                "",
                "FILTER_XBR",
                "FILTER_BILINEAR",
                "FILTER_3POINT",
                "FILTER_JINC2",
                "FILTER_SABR"
            }}
        }} }}
    },
    Program{
        {"primitive.vert", "primitive_feedback.frag"},
        AllOf{{
            OneOf{{ "", "TEXTURED" }},
            OneOf{{ "", "MSAA" }},
            OneOf{{ "BLEND_ADD", "BLEND_AVG", "BLEND_SUB", "BLEND_ADD_QUARTER" }}
        }}
    },
    Program{
        {"resolve.comp"},
        OneOf{{
            AllOf{{ "SCALED", OneOf{{ "", "MSAA" }} }},
            AllOf{{ "UNSCALED", OneOfVal{ "SCALE", { 2, 4, 8, 16 }} }}
        }}
    },
    Program{
        {"quad.vert", "quad.frag"},
        OneOf{{
            AllOf{{
                "UNSCALED",
                OneOf{{ "", "DITHER", AllOf{{ "BPP24", OneOf{{ "", "BPP24_YUV", }} }} }},
            }},
            AllOf{{
                "SCALED",
                OneOf{{ "", "DITHER" }},
            }}
        }}
    },
    Program{
        {"copy_vram.comp"},
        OneOf{{ "", "MASKED" }}
    },
    Program{
        {"blit_vram.comp", "blit_vram_cached.comp"},
        OneOf{{
            AllOf{{ "SCALED", OneOf{{ "", "MASKED" }}, OneOf{{ "", "MSAA" }} }},
            AllOf{{ "UNSCALED", OneOf{{ "", "MASKED" }} }}
        }},    
    },
    Program{
        {"mipmap.vert"},
        OneOf{{ "", "SHIFT_QUAD" }}
    },
    Program{
        {"mipmap_resolve.frag"},
        OneOf{{ "", "DITHER" }}
    },
    Program{
        {"mipmap_energy.frag"},
        OneOf{{ "", "FIRST_PASS", }}
    },
    Program{{"mipmap_blur.frag"}}
}};
