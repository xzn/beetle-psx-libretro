#include "rebuild_shaders.h"

// prog: <file.vert>, <file.frag>, (<def>|<opt>)*
// prog: <file.comp>, (<def>|<opt>)*

// one of
// def: (<string>|<def>|<opt>)*

// combinations of
// opt: (<string>|<def>|<opt>)*

const ShaderList glsl_shader_list = shaders(
    prog(
        "primitive.vert", "primitive.frag",
        def(
            "",
            opt(
                "TEXTURED",
                def(
                    "OPAQUE",
                    "SEMI_TRANS_OPAQUE",
                    "SEMI_TRANS"
                ),
                def(
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
