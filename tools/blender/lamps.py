# dendrite's red parts as real objects: a ruby bead (the chosen itch length) and a jewel
# pilot lamp in a black bezel (the heads). Renders RGBA sprites, top-down, off and lit.
#   blender -b -noaudio --python tools/blender/lamps.py -- bead|lamp off|on Resources/lamps/<obj>-<state>.png 128
import bpy, bmesh, sys, math
argv = sys.argv[sys.argv.index("--") + 1:]
obj, state, out = argv[0], argv[1], argv[2]
size = int(argv[3]) if len(argv) > 3 else 192
lit = state == "on"

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'
sc.cycles.samples = 512; sc.cycles.use_denoising = False
sc.cycles.max_bounces = 16; sc.cycles.transmission_bounces = 16; sc.cycles.glossy_bounces = 8
sc.cycles.caustics_refractive = True; sc.cycles.caustics_reflective = True
sc.render.resolution_x = sc.render.resolution_y = size
sc.render.film_transparent = True
sc.render.image_settings.file_format = 'PNG'; sc.render.image_settings.color_mode = 'RGBA'
sc.view_settings.view_transform = 'Standard'
sc.view_settings.look = 'None'
sc.render.filepath = out

# a dark studio: a long softbox overhead, a little behind the top of the window, and a faint sky
world = bpy.data.worlds.new("w"); sc.world = world; world.use_nodes = True
bg = world.node_tree.nodes["Background"]; bg.inputs[0].default_value = (1, 1, 1, 1); bg.inputs[1].default_value = 0.02

def area(name, loc, rot, sx, sy, energy, colour=(1, 1, 1)):
    l = bpy.data.lights.new(name, 'AREA'); l.shape = 'RECTANGLE'; l.size = sx; l.size_y = sy
    l.energy = energy; l.color = colour
    o = bpy.data.objects.new(name, l); sc.collection.objects.link(o); o.location = loc; o.rotation_euler = rot
    return o

def disk(name, loc, size, energy, colour=(1, 1, 1)):
    l = bpy.data.lights.new(name, 'AREA'); l.shape = 'DISK'; l.size = size; l.energy = energy; l.color = colour
    o = bpy.data.objects.new(name, l); sc.collection.objects.link(o); o.location = loc
    c = o.constraints.new('TRACK_TO'); t = bpy.data.objects.new(name + "-t", None); sc.collection.objects.link(t)
    c.target = t; c.track_axis = 'TRACK_NEGATIVE_Z'; c.up_axis = 'UP_Y'
    return o

disk("softbox", (-1.6, 2.2, 3.4), 3.0, 260)   # a big soft window up and to the left
disk("spark", (-0.6, 0.9, 4.0), 0.15, 6)       # a small hard light for the glint
disk("rim", (2.2, -2.0, 0.8), 1.2, 25)         # a cool edge from below right

cam = bpy.data.cameras.new("c"); cam.type = 'ORTHO'; cam.ortho_scale = 2.6
co = bpy.data.objects.new("c", cam); sc.collection.objects.link(co); sc.camera = co
co.location = (0, 0, 5)

def mat(name):
    m = bpy.data.materials.new(name); m.use_nodes = True
    return m, m.node_tree.nodes, m.node_tree.links

def glass_red(name, depth, glow=0.0):
    # red glass: clear surface, colour from absorption in the volume, so thick parts go deep
    m, N, L = mat(name)
    for n in list(N): N.remove(n)
    o = N.new("ShaderNodeOutputMaterial")
    g = N.new("ShaderNodeBsdfGlass"); g.inputs["Color"].default_value = (1, 0.92, 0.9, 1)
    g.inputs["Roughness"].default_value = 0.015; g.inputs["IOR"].default_value = 1.52
    v = N.new("ShaderNodeVolumeAbsorption"); v.inputs["Color"].default_value = (0.85, 0.015, 0.01, 1)
    v.inputs["Density"].default_value = depth
    L.new(g.outputs[0], o.inputs["Surface"])
    if glow > 0:
        # lit: the glass itself glows from within
        e = N.new("ShaderNodeEmission"); e.inputs["Color"].default_value = (1.0, 0.05, 0.02, 1)
        e.inputs["Strength"].default_value = glow
        a = N.new("ShaderNodeAddShader"); L.new(v.outputs[0], a.inputs[0]); L.new(e.outputs[0], a.inputs[1])
        L.new(a.outputs[0], o.inputs["Volume"])
    else:
        L.new(v.outputs[0], o.inputs["Volume"])
    return m

def emitter(name, colour, strength):
    m, N, L = mat(name)
    for n in list(N): N.remove(n)
    o = N.new("ShaderNodeOutputMaterial"); e = N.new("ShaderNodeEmission")
    e.inputs["Color"].default_value = (*colour, 1); e.inputs["Strength"].default_value = strength
    L.new(e.outputs[0], o.inputs["Surface"])
    return m

def diffuse(name, colour, rough=0.6, metal=0.0):
    m, N, L = mat(name)
    b = N["Principled BSDF"]; b.inputs["Base Color"].default_value = (*colour, 1)
    b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
    return m

def add(o, m):
    o.data.materials.append(m)
    for p in o.data.polygons:
        p.use_smooth = True
    return o

# something under the glass for it to refract (the camera does not see it)
bpy.ops.mesh.primitive_plane_add(size=12, location=(0, 0, -1.0))
fl = add(bpy.context.object, diffuse("floor", (0.06, 0.06, 0.06), 0.5))
fl.visible_camera = False

if obj == "bead":
    # a polished ruby bead with an ember inside
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.0, segments=96, ring_count=48)
    bead = add(bpy.context.object, glass_red("ruby", 1.1 if lit else 1.8))
    bead.scale = (1, 1, 0.8)
    if lit:
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.42, location=(0.0, 0.0, -0.1))
        add(bpy.context.object, emitter("ember", (1.0, 0.05, 0.02), 1.7))
else:
    # a pilot lamp: a faceted red jewel in a black anodised bezel, a reflector cup behind
    bpy.ops.mesh.primitive_torus_add(major_radius=0.88, minor_radius=0.14, major_segments=128, minor_segments=32)
    ring = add(bpy.context.object, diffuse("bezel", (0.035, 0.035, 0.038), 0.25, 1.0))
    ring.scale = (1, 1, 0.7)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.8, segments=96, ring_count=48, location=(0, 0, -0.42))
    cup = add(bpy.context.object, diffuse("reflector", (0.55, 0.12, 0.1), 0.3, 1.0))
    cup.scale = (1, 1, 0.35)
    bpy.ops.mesh.primitive_ico_sphere_add(radius=0.76, subdivisions=2, location=(0, 0, -0.04))
    jewel = bpy.context.object
    jewel.data.materials.append(glass_red("jewel", 1.8 if lit else 2.8, 1.7 if lit else 0.0))
    jewel.scale = (1, 1, 0.6)
    if lit:
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.22, location=(0, 0, -0.3))
        add(bpy.context.object, emitter("filament", (1.0, 0.06, 0.02), 2.2))
        l = bpy.data.lights.new("in", 'POINT'); l.energy = 8; l.color = (1.0, 0.12, 0.04); l.shadow_soft_size = 0.2
        o = bpy.data.objects.new("in", l); sc.collection.objects.link(o); o.location = (0, 0, -0.25)

bpy.ops.render.render(write_still=True)
