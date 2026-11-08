# Blender: blender -b -noaudio --python tools/snapshot/bake_bar.py -- slate|metal|tissue out.png
# Then grade it dark (mean ~5%, a hairline at the top edge) and save as Resources/textures/bar.png.
# Bakes the bottom bar (880x72 design px, rendered at 2x) as a lit material close-up.
import bpy, sys, math
argv = sys.argv[sys.argv.index("--") + 1:]
kind, out = argv[0], argv[1]
W, H = 1760, 144

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'CYCLES'
sc.cycles.device = 'CPU'
sc.cycles.samples = 256
try:
    sc.cycles.use_denoising = False
except Exception:
    pass
sc.render.resolution_x, sc.render.resolution_y = W, H
sc.render.film_transparent = False
sc.view_settings.view_transform = 'Standard'
sc.view_settings.look = 'None'
sc.render.image_settings.file_format = 'PNG'
sc.render.filepath = out

world = bpy.data.worlds.new("w"); sc.world = world; world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0, 0, 0, 1)
# a sky that is brighter towards the top of the window: slopes facing up catch it
wn = world.node_tree.nodes; wl = world.node_tree.links
bg = wn["Background"]; bg.inputs[0].default_value = (1, 1, 1, 1)
wtc = wn.new("ShaderNodeTexCoord"); sep = wn.new("ShaderNodeSeparateXYZ")
wl.new(wtc.outputs["Generated"], sep.inputs[0])
mr = wn.new("ShaderNodeMapRange"); mr.inputs["From Min"].default_value = 0.08; mr.inputs["From Max"].default_value = 0.6
mr.inputs["To Min"].default_value = 0.01; mr.inputs["To Max"].default_value = 1.2
wl.new(sep.outputs["Y"], mr.inputs["Value"]); wl.new(mr.outputs[0], bg.inputs[1])

aspect = W / H
bpy.ops.mesh.primitive_plane_add(size=1)
plane = bpy.context.object
plane.scale = (aspect, 1, 1)
bpy.ops.object.transform_apply(scale=True)

cam = bpy.data.cameras.new("c"); cam.type = 'ORTHO'; cam.ortho_scale = aspect
co = bpy.data.objects.new("c", cam); sc.collection.objects.link(co); sc.camera = co
co.location = (0, 0, 3)

def sun(elev, azim, energy, soft=0.15):
    l = bpy.data.lights.new("sun", 'SUN'); l.energy = energy; l.angle = soft
    o = bpy.data.objects.new("sun", l); sc.collection.objects.link(o)
    o.rotation_euler = (-math.radians(90 - elev), 0, math.radians(azim))
    return o

def light(name, loc, rot, size, energy, sx=None):
    l = bpy.data.lights.new(name, 'AREA'); l.energy = energy
    l.shape = 'RECTANGLE'; l.size = size; l.size_y = sx or size
    o = bpy.data.objects.new(name, l); sc.collection.objects.link(o)
    o.location = loc; o.rotation_euler = rot
    return o

m = bpy.data.materials.new(kind); m.use_nodes = True
nt = m.node_tree; N = nt.nodes; L = nt.links
for n in list(N): N.remove(n)
outn = N.new("ShaderNodeOutputMaterial")
bs = N.new("ShaderNodeBsdfPrincipled"); L.new(bs.outputs[0], outn.inputs[0])
tc = N.new("ShaderNodeTexCoord")
def mapping(sx, sy, sz=1):
    mp = N.new("ShaderNodeMapping"); mp.inputs["Scale"].default_value = (sx, sy, sz)
    L.new(tc.outputs["Object"], mp.inputs[0]); return mp
def noise(scale, detail, rough, vec=None):
    n = N.new("ShaderNodeTexNoise"); n.inputs["Scale"].default_value = scale
    n.inputs["Detail"].default_value = detail; n.inputs["Roughness"].default_value = rough
    if vec is not None: L.new(vec.outputs[0], n.inputs["Vector"])
    else: L.new(tc.outputs["Object"], n.inputs["Vector"])
    return n
def ramp(src, a, b, pa=0.0, pb=1.0):
    r = N.new("ShaderNodeValToRGB"); e = r.color_ramp.elements
    e[0].position, e[1].position = pa, pb
    e[0].color = (a, a, a, 1); e[1].color = (b, b, b, 1)
    L.new(src, r.inputs[0]); return r
def bump(src, strength, dist=0.002, normal=None):
    b = N.new("ShaderNodeBump"); b.inputs["Strength"].default_value = strength
    b.inputs["Distance"].default_value = dist
    L.new(src, b.inputs["Height"])
    if normal is not None: L.new(normal.outputs[0], b.inputs["Normal"])
    return b

if kind == "slate":
    # honed black slate: a fine mineral grain, faint cleavage veins, a dull sheen
    grain = noise(160, 6, 0.7)
    cloud = noise(2.2, 6, 0.6)
    fleck = noise(40, 3, 0.5)
    vein = noise(2.5, 10, 0.65, mapping(1, 7))
    col = ramp(cloud.outputs["Fac"], 0.010, 0.024, 0.35, 0.65)
    vc = ramp(vein.outputs["Fac"], 0.0, 1.0, 0.497, 0.500)   # thin pale veins
    vm = N.new("ShaderNodeMix"); vm.data_type = 'RGBA'; vm.blend_type = 'LIGHTEN'
    vm.inputs["Factor"].default_value = 0.0
    L.new(col.outputs[0], vm.inputs[6]); L.new(vc.outputs[0], vm.inputs[7])
    L.new(vm.outputs[2], bs.inputs["Base Color"])
    rr = ramp(grain.outputs["Fac"], 0.42, 0.7, 0.3, 0.7); L.new(rr.outputs[0], bs.inputs["Roughness"])
    b1 = bump(grain.outputs["Fac"], 0.35)
    b2 = bump(fleck.outputs["Fac"], 0.25, 0.004, b1)
    L.new(b2.outputs[0], bs.inputs["Normal"])
    sun(12, 25, 3.0)
elif kind == "metal":
    # bead-blasted then brushed black anodised aluminium, like a rack panel
    brushed = noise(1.0, 6, 0.6, mapping(0.25, 160.0))
    blast = noise(300, 4, 0.5)
    bs.inputs["Metallic"].default_value = 1.0
    bs.inputs["Base Color"].default_value = (0.06, 0.06, 0.062, 1)
    rr = ramp(brushed.outputs["Fac"], 0.26, 0.5, 0.3, 0.7); L.new(rr.outputs[0], bs.inputs["Roughness"])
    try:
        bs.inputs["Anisotropic"].default_value = 0.85
    except Exception:
        pass
    b1 = bump(brushed.outputs["Fac"], 0.5)
    b2 = bump(blast.outputs["Fac"], 0.06, 0.001, b1)
    L.new(b2.outputs[0], bs.inputs["Normal"])
    tg = N.new("ShaderNodeTangent"); tg.direction_type = 'UV_MAP'
    L.new(tg.outputs[0], bs.inputs["Tangent"])
    light("softbox", (0.0, 0.9, 2.5), (0, 0, 0), 40, 260, 1.6)
elif kind == "tissue":
    # a wet membrane seen close: soft cells, a film of moisture, nearly black
    vor = N.new("ShaderNodeTexVoronoi"); vor.feature = 'SMOOTH_F1'
    vor.inputs["Scale"].default_value = 6
    warp = noise(3, 4, 0.5)
    mix = N.new("ShaderNodeMix"); mix.data_type = 'VECTOR'; mix.inputs["Factor"].default_value = 0.18
    L.new(tc.outputs["Object"], mix.inputs[4]); L.new(warp.outputs["Color"], mix.inputs[5])
    L.new(mix.outputs[1], vor.inputs["Vector"])
    edge = ramp(vor.outputs["Distance"], 1.0, 0.0, 0.0, 0.6)
    pores = noise(220, 3, 0.5)
    cloud = noise(2, 5, 0.6)
    col = ramp(cloud.outputs["Fac"], 0.010, 0.03, 0.3, 0.7)
    L.new(col.outputs[0], bs.inputs["Base Color"])
    bs.inputs["Roughness"].default_value = 0.55
    for nm, v in (("Coat Weight", 1.0), ("Coat Roughness", 0.06)):
        try: bs.inputs[nm].default_value = v
        except Exception: pass
    b0 = bump(pores.outputs["Fac"], 0.08, 0.001)
    b1 = bump(edge.outputs[0], 1.0, 0.05, b0)
    L.new(b1.outputs[0], bs.inputs["Normal"])
    try: L.new(b1.outputs[0], bs.inputs["Coat Normal"])
    except Exception: pass
    sun(18, 30, 1.2, 0.3)

plane.data.materials.append(m)
bpy.ops.render.render(write_still=True)
