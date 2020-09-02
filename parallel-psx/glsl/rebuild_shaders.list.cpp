#include "rebuild_shaders.h"

// If the short description below looks confusing just copy and paste,
// it should just works.

// shaders: <prog>, *
// prog: <file.vert>, <file.frag>, <def>
// prog: <file.comp>, <def>
// one_of: <def>
// all_of: <def>
// <def>: (<string> | <one_of> | <all_of>), *

const ShaderList glsl_shader_list = shaders(
    prog(
        "primitive.vert", "primitive.frag",
        one_of(
            "",
            all_of(
                "TEXTURED",
                one_of(
                    "OPAQUE",
                    "SEMI_TRANS_OPAQUE",
                    "SEMI_TRANS"
                ),
                one_of(
                    "",
                    "FILTER_XBR",
                    "FILTER_BILINEAR",
                    "FILTER_3POINT",
                    "FILTER_JINC2",
                    "FILTER_SABR"
                )
            )
        )
    )
);
