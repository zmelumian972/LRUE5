# The Classic look's materials, made in the editor:
#   UnrealEditor-Cmd LegoRacers.uproject -run=pythonscript -script="$PWD/scripts/make_classic_materials.py" -unattended
# Every one unlit, colour = texture.rgb * vertex.rgb^2.2 * Tint.rgb (the vertex bytes decoded per pixel, after
# interpolation: the original's Gouraud ran in gamma space), its texture coordinates offset by UVOffset (texture scroll,
# environment mapping):
#   /Game/LR/Materials/M_ClassicMasked       opaque, texels cut at opacity < 1/3 (the colour keys)
#   /Game/LR/Materials/M_ClassicTranslucent  blend: opacity = texture.a * vertex.a * Alpha, applied as the original's
#                                            gamma-space blend would look; pulled toward the eye by Pull
#   /Game/LR/Materials/M_ClassicAdditive     additive (the glows): colour * opacity
import unreal

PATH = "/Game/LR/Materials"
lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def node(m, cls, x, y):
    return lib.create_material_expression(m, cls, x, y)


def make(name, blend):
    full = f"{PATH}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        unreal.EditorAssetLibrary.delete_asset(full)
    m = tools.create_asset(name, PATH, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", blend)
    m.set_editor_property("two_sided", False)
    if blend == unreal.BlendMode.BLEND_MASKED:
        m.set_editor_property("opacity_mask_clip_value", 1.0 / 3.0)

    uv = node(m, unreal.MaterialExpressionTextureCoordinate, -1400, 0)
    offset = node(m, unreal.MaterialExpressionVectorParameter, -1400, 150)
    offset.set_editor_property("parameter_name", "UVOffset")
    offset.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 0))
    offset_rg = node(m, unreal.MaterialExpressionComponentMask, -1200, 150)
    offset_rg.set_editor_property("r", True)
    offset_rg.set_editor_property("g", True)
    offset_rg.set_editor_property("b", False)
    offset_rg.set_editor_property("a", False)
    lib.connect_material_expressions(offset, "", offset_rg, "")
    add_uv = node(m, unreal.MaterialExpressionAdd, -1000, 50)
    lib.connect_material_expressions(uv, "", add_uv, "A")
    lib.connect_material_expressions(offset_rg, "", add_uv, "B")

    tex = node(m, unreal.MaterialExpressionTextureSampleParameter2D, -800, 0)
    tex.set_editor_property("parameter_name", "Tex")
    tex.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    lib.connect_material_expressions(add_uv, "", tex, "UVs")

    vc = node(m, unreal.MaterialExpressionVertexColor, -800, 300)
    gamma = node(m, unreal.MaterialExpressionConstant, -800, 450)
    gamma.set_editor_property("r", 2.2)
    vc_lin = node(m, unreal.MaterialExpressionPower, -600, 300)
    lib.connect_material_expressions(vc, "", vc_lin, "Base")
    lib.connect_material_expressions(gamma, "", vc_lin, "Exp")
    vc_rgb = vc_lin

    tint = node(m, unreal.MaterialExpressionVectorParameter, -800, 550)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    tint_rgb = node(m, unreal.MaterialExpressionComponentMask, -600, 550)
    for c in ("r", "g", "b"):
        tint_rgb.set_editor_property(c, True)
    tint_rgb.set_editor_property("a", False)
    lib.connect_material_expressions(tint, "", tint_rgb, "")

    colour = node(m, unreal.MaterialExpressionMultiply, -250, 100)
    lib.connect_material_expressions(tex, "RGB", colour, "A")
    lib.connect_material_expressions(vc_rgb, "", colour, "B")
    colour2 = node(m, unreal.MaterialExpressionMultiply, -100, 150)
    lib.connect_material_expressions(colour, "", colour2, "A")
    lib.connect_material_expressions(tint_rgb, "", colour2, "B")

    # Opacity: texture alpha * vertex alpha (bytes, linear) * Alpha.
    alpha = node(m, unreal.MaterialExpressionScalarParameter, -450, 650)
    alpha.set_editor_property("parameter_name", "Alpha")
    alpha.set_editor_property("default_value", 1.0)
    op1 = node(m, unreal.MaterialExpressionMultiply, -250, 400)
    lib.connect_material_expressions(tex, "A", op1, "A")
    lib.connect_material_expressions(vc, "A", op1, "B")
    opacity = node(m, unreal.MaterialExpressionMultiply, -100, 450)
    lib.connect_material_expressions(op1, "", opacity, "A")
    lib.connect_material_expressions(alpha, "", opacity, "B")

    if blend == unreal.BlendMode.BLEND_MASKED:
        lib.connect_material_property(colour2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        lib.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    elif blend == unreal.BlendMode.BLEND_ADDITIVE:
        glow = node(m, unreal.MaterialExpressionMultiply, 100, 200)
        lib.connect_material_expressions(colour2, "", glow, "A")
        lib.connect_material_expressions(opacity, "", glow, "B")
        lib.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        # The original blended gamma values; UE blends linear ones. opacity' = lerp(1 - (1 - a)^2.2, a^2.2, luma^(1/2.2))
        # with the source's luma.
        lib.connect_material_property(colour2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        one = node(m, unreal.MaterialExpressionConstant, 100, 500)
        one.set_editor_property("r", 1.0)
        inv = node(m, unreal.MaterialExpressionOneMinus, 100, 600)
        lib.connect_material_expressions(opacity, "", inv, "")
        inv_p = node(m, unreal.MaterialExpressionPower, 250, 600)
        lib.connect_material_expressions(inv, "", inv_p, "Base")
        lib.connect_material_expressions(gamma, "", inv_p, "Exp")
        dark = node(m, unreal.MaterialExpressionOneMinus, 400, 600)
        lib.connect_material_expressions(inv_p, "", dark, "")
        bright = node(m, unreal.MaterialExpressionPower, 250, 450)
        lib.connect_material_expressions(opacity, "", bright, "Base")
        lib.connect_material_expressions(gamma, "", bright, "Exp")
        weights = node(m, unreal.MaterialExpressionConstant3Vector, 100, 750)
        weights.set_editor_property("constant", unreal.LinearColor(0.3, 0.59, 0.11, 0))
        luma = node(m, unreal.MaterialExpressionDotProduct, 250, 750)
        lib.connect_material_expressions(colour2, "", luma, "A")
        lib.connect_material_expressions(weights, "", luma, "B")
        luma_s = node(m, unreal.MaterialExpressionSaturate, 400, 750)
        lib.connect_material_expressions(luma, "", luma_s, "")
        inv_gamma = node(m, unreal.MaterialExpressionConstant, 400, 850)
        inv_gamma.set_editor_property("r", 1.0 / 2.2)
        luma_g = node(m, unreal.MaterialExpressionPower, 550, 750)
        lib.connect_material_expressions(luma_s, "", luma_g, "Base")
        lib.connect_material_expressions(inv_gamma, "", luma_g, "Exp")
        mix = node(m, unreal.MaterialExpressionLinearInterpolate, 700, 550)
        lib.connect_material_expressions(dark, "", mix, "A")
        lib.connect_material_expressions(bright, "", mix, "B")
        lib.connect_material_expressions(luma_g, "", mix, "Alpha")
        lib.connect_material_property(mix, "", unreal.MaterialProperty.MP_OPACITY)

    if blend != unreal.BlendMode.BLEND_MASKED:
        # Pulled toward the eye: world position offset = (camera - position) * Pull.
        cam = node(m, unreal.MaterialExpressionCameraPositionWS, 100, 1000)
        wp = node(m, unreal.MaterialExpressionWorldPosition, 100, 1100)
        to_eye = node(m, unreal.MaterialExpressionSubtract, 300, 1050)
        lib.connect_material_expressions(cam, "", to_eye, "A")
        lib.connect_material_expressions(wp, "", to_eye, "B")
        pull = node(m, unreal.MaterialExpressionScalarParameter, 300, 1200)
        pull.set_editor_property("parameter_name", "Pull")
        pull.set_editor_property("default_value", 0.0)
        wpo = node(m, unreal.MaterialExpressionMultiply, 500, 1100)
        lib.connect_material_expressions(to_eye, "", wpo, "A")
        lib.connect_material_expressions(pull, "", wpo, "B")
        lib.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    m.set_editor_property("used_with_static_lighting", False)
    lib.recompile_material(m)
    unreal.EditorAssetLibrary.save_asset(full)
    unreal.log(f"LRMaterials: {full}")


make("M_ClassicMasked", unreal.BlendMode.BLEND_MASKED)
make("M_ClassicTranslucent", unreal.BlendMode.BLEND_TRANSLUCENT)
make("M_ClassicAdditive", unreal.BlendMode.BLEND_ADDITIVE)
