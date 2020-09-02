#include "rebuild_shaders.h"

// If the short description below looks confusing just copy and paste,
// it should just works.

// prog: <file.vert>, <file.frag>, (<one_of>|<all_of>)*
// prog: <file.comp>, (<one_of>|<all_of>)*
// one_of: (<string>|<one_of>|<all_of>)*
// all_of: (<string>|<one_of>|<all_of>)*

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
