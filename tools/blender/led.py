# dendrite's red lights as a real device in a blackout room: there is no light but the LED's
# own. A diffused red LED sits in a small chamfered hole in a matte black panel; lit, it
# glows and spills a faint red pool onto the panel around it; unlit, it is almost gone.
#   blender -b -noaudio --python tools/blender/led.py -- on out.png 256 1.5 900 -3.5
#   blender -b -noaudio --python tools/blender/led.py -- off out.png 256 0 0 0
# then alpha = max(r, g, b) and colour = rgb / alpha, saved to Resources/lamps/led-on|off.png
import bpy, sys, math
argv = sys.argv[sys.argv.index("--") + 1:]
state, out = argv[0], argv[1]
size = int(argv[2]) if len(argv) > 2 else 256
lit = state == "on"
GLOW = float(argv[3]) if len(argv) > 3 else 5.0
DIE = float(argv[4]) if len(argv) > 4 else 40.0

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'
sc.cycles.samples = 640; sc.cycles.use_denoising = False
sc.cycles.max_bounces = 12; sc.cycles.transmission_bounces = 12
sc.render.resolution_x = sc.render.resolution_y = size
sc.render.film_transparent = False
sc.render.image_settings.file_format = 'PNG'; sc.render.image_settings.color_mode = 'RGB'
sc.render.image_settings.color_depth = '16'
sc.view_settings.view_transform = 'AgX'
sc.view_settings.exposure = float(argv[5]) if len(argv) > 5 else 0.0
try:
    sc.view_settings.look = 'AgX - Punchy'
except Exception as e:
    print("look", e)
sc.render.filepath = out

world = bpy.data.worlds.new("w"); sc.world = world; world.use_nodes = True
bg = world.node_tree.nodes["Background"]; bg.inputs[0].default_value = (1, 1, 1, 1)
bg.inputs[1].default_value = 0.0 if lit else 0.03   # unlit: only the faintest stray light of the room

cam = bpy.data.cameras.new("c"); cam.type = 'ORTHO'; cam.ortho_scale = 4.0
co = bpy.data.objects.new("c", cam); sc.collection.objects.link(co); sc.camera = co
co.location = (0, 0, 6)

def mat(name):
    m = bpy.data.materials.new(name); m.use_nodes = True
    return m, m.node_tree.nodes, m.node_tree.links

def smooth(o):
    for p in o.data.polygons:
        p.use_smooth = True
    return o

# the panel: matte black, bead-blasted, a fine grain the light rakes across
m, N, L = mat("panel"); b = N["Principled BSDF"]
b.inputs["Base Color"].default_value = (0.035, 0.035, 0.036, 1); b.inputs["Roughness"].default_value = 0.38
nz = N.new("ShaderNodeTexNoise"); nz.inputs["Scale"].default_value = 260; nz.inputs["Detail"].default_value = 6
bu = N.new("ShaderNodeBump"); bu.inputs["Strength"].default_value = 0.12; bu.inputs["Distance"].default_value = 0.002
L.new(nz.outputs["Fac"], bu.inputs["Height"]); L.new(bu.outputs[0], b.inputs["Normal"])
bpy.ops.mesh.primitive_plane_add(size=20, location=(0, 0, 0))
panel = bpy.context.object; panel.data.materials.append(m)

# a hole through the panel with a bright-cut chamfer (cut the plane with a boolean)
bpy.ops.mesh.primitive_cylinder_add(radius=0.42, depth=1.0, vertices=128, location=(0, 0, 0))
cut = bpy.context.object
mod = panel.modifiers.new("hole", 'BOOLEAN'); mod.object = cut; mod.operation = 'DIFFERENCE'
bpy.context.view_layer.objects.active = panel; bpy.ops.object.modifier_apply(modifier="hole")
bpy.data.objects.remove(cut)
m2, N2, L2 = mat("chamfer"); b2 = N2["Principled BSDF"]
b2.inputs["Base Color"].default_value = (0.5, 0.5, 0.5, 1); b2.inputs["Metallic"].default_value = 1.0
b2.inputs["Roughness"].default_value = 0.22
bpy.ops.mesh.primitive_cone_add(vertices=128, radius1=0.42, radius2=0.5, depth=0.08, end_fill_type='NOTHING', location=(0, 0, -0.04))
ch = smooth(bpy.context.object); ch.data.materials.append(m2)
bpy.ops.mesh.primitive_cylinder_add(radius=0.42, depth=0.4, vertices=128, end_fill_type='NOTHING', location=(0, 0, -0.28))
wall = smooth(bpy.context.object)
m3, N3, L3 = mat("wall"); N3["Principled BSDF"].inputs["Base Color"].default_value = (0.02, 0.02, 0.02, 1)
N3["Principled BSDF"].inputs["Roughness"].default_value = 0.35; wall.data.materials.append(m3)

# the LED: a frosted red dome sitting just below the panel face, the die glowing inside it
m4, N4, L4 = mat("lens"); 
for n in list(N4): N4.remove(n)
o4 = N4.new("ShaderNodeOutputMaterial")
gl = N4.new("ShaderNodeBsdfGlass"); gl.inputs["Color"].default_value = (1.0, 0.1, 0.07, 1)
gl.inputs["Roughness"].default_value = 0.45; gl.inputs["IOR"].default_value = 1.5
tr = N4.new("ShaderNodeBsdfTranslucent"); tr.inputs["Color"].default_value = (0.85, 0.012, 0.006, 1)
mx = N4.new("ShaderNodeMixShader"); mx.inputs[0].default_value = 0.75
L4.new(gl.outputs[0], mx.inputs[1]); L4.new(tr.outputs[0], mx.inputs[2]); L4.new(mx.outputs[0], o4.inputs["Surface"])
if lit:
    # lit, the frosted dome is itself the light: brightest where you look straight into it
    lw = N4.new("ShaderNodeLayerWeight"); lw.inputs["Blend"].default_value = 0.35
    rp = N4.new("ShaderNodeValToRGB"); e = rp.color_ramp.elements
    e[0].position = 0.0; e[0].color = (1, 1, 1, 1); e[1].position = 1.0; e[1].color = (0.08, 0.08, 0.08, 1)
    L4.new(lw.outputs["Facing"], rp.inputs[0])
    em = N4.new("ShaderNodeEmission"); em.inputs["Color"].default_value = (1.0, 0.018, 0.006, 1)
    ms = N4.new("ShaderNodeMath"); ms.operation = 'MULTIPLY'; ms.inputs[1].default_value = GLOW
    L4.new(rp.outputs[0], ms.inputs[0]); L4.new(ms.outputs[0], em.inputs["Strength"])
    ad = N4.new("ShaderNodeAddShader"); L4.new(mx.outputs[0], ad.inputs[0]); L4.new(em.outputs[0], ad.inputs[1])
    L4.new(ad.outputs[0], o4.inputs["Surface"])
bpy.ops.mesh.primitive_uv_sphere_add(radius=0.36, segments=96, ring_count=48, location=(0, 0, 0.0))
lens = smooth(bpy.context.object); lens.scale = (1, 1, 0.62); lens.data.materials.append(m4)

if lit:
    m5, N5, L5 = mat("die")
    for n in list(N5): N5.remove(n)
    o5 = N5.new("ShaderNodeOutputMaterial"); e5 = N5.new("ShaderNodeEmission")
    e5.inputs["Color"].default_value = (1.0, 0.05, 0.015, 1); e5.inputs["Strength"].default_value = DIE
    L5.new(e5.outputs[0], o5.inputs["Surface"])
    bpy.ops.mesh.primitive_cube_add(size=0.07, location=(0, 0, 0.0))
    bpy.context.object.data.materials.append(m5)

# the camera's own bloom: a soft glow around anything bright, as a lens sees it in the dark
sc.use_nodes = True
ct = sc.node_tree
for n in list(ct.nodes): ct.nodes.remove(n)
rl = ct.nodes.new("CompositorNodeRLayers"); comp = ct.nodes.new("CompositorNodeComposite")
gl1 = ct.nodes.new("CompositorNodeGlare"); gl1.glare_type = 'FOG_GLOW'; gl1.quality = 'HIGH'; gl1.size = 9
gl1.threshold = 0.05; gl1.mix = 0.35
ct.links.new(rl.outputs["Image"], gl1.inputs["Image"]); ct.links.new(gl1.outputs["Image"], comp.inputs["Image"])

bpy.ops.render.render(write_still=True)
