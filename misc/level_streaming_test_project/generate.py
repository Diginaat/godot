#!/usr/bin/env python3
# Writes the street, shop and plaza cell scenes, their textures and the main
# scene of the level streaming test project. The generated files are
# committed; run this again only to change the layout:
#
#     python3 misc/level_streaming_test_project/generate.py
#
# Needs Pillow for the textures.

import os
import random

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.abspath(__file__))
STREET_CELLS = 10  # street_00 .. street_09, 20 m each along +Z.
CELL_LENGTH = 20
SHOP_STREET = 3  # The shop opens onto this street cell (its +X side).
# Extra scenes made like a street cell: the plaza's annex and the depot.
EXTRA = {"annex": 10, "depot": 11}


def write(path, text):
    full = os.path.join(ROOT, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w") as f:
        f.write(text)


def color(index, saturation=0.8, value=0.9):
    import colorsys

    r, g, b = colorsys.hsv_to_rgb((index * 0.137) % 1.0, saturation, value)
    return r, g, b


def make_sign(index, name):
    # A 512x512 shop sign, different for every cell, so each cell loads and
    # uploads a texture of its own.
    rng = random.Random(index)
    r, g, b = color(index)
    image = Image.new("RGB", (512, 512), (int(r * 60), int(g * 60), int(b * 60)))
    draw = ImageDraw.Draw(image)
    for _ in range(24):
        x, y = rng.randrange(512), rng.randrange(512)
        s = rng.randrange(20, 120)
        c = color(index + rng.randrange(3), 0.9, 1.0)
        draw.ellipse((x, y, x + s, y + s), outline=tuple(int(v * 255) for v in c), width=6)
    try:
        font = ImageFont.load_default(size=150)
    except TypeError:
        font = ImageFont.load_default()
    draw.text((40, 170), name, fill=(255, 255, 255), font=font)
    image.save(os.path.join(ROOT, "textures", name + ".png"))


def make_asphalt():
    rng = random.Random(1)
    image = Image.new("RGB", (256, 256))
    pixels = image.load()
    for y in range(256):
        for x in range(256):
            v = 40 + rng.randrange(30)
            pixels[x, y] = (v, v, v + 4)
    image.save(os.path.join(ROOT, "textures", "asphalt.png"))


def navmesh(polygons):
    # polygons: lists of (x, z) corners, clockwise seen from above.
    vertices = []
    indices = []
    for polygon in polygons:
        ids = []
        for x, z in polygon:
            if (x, z) not in vertices:
                vertices.append((x, z))
            ids.append(vertices.index((x, z)))
        indices.append(ids)
    v = ", ".join(f"{x}, 0, {z}" for x, z in vertices)
    p = ", ".join("PackedInt32Array(" + ", ".join(str(i) for i in ids) + ")" for ids in indices)
    return f'[sub_resource type="NavigationMesh" id="nav"]\nvertices = PackedVector3Array({v})\npolygons = [{p}]\n'


def quad(x0, z0, x1, z1):
    return [(x0, z0), (x1, z0), (x1, z1), (x0, z1)]


def street_scene(index, name, shop_side):
    r, g, b = color(index, 0.35, 0.55)
    lr, lg, lb = color(index, 0.9, 1.0)
    if shop_side:
        # The shop's doorway is at x = 10, z = -8 .. 8 of this cell: split the
        # road so the shop's edge meets an edge of the same length.
        nav = navmesh([quad(-10, -10, 10, -8), quad(-10, -8, 10, 8), quad(-10, 8, 10, 10)])
    else:
        nav = navmesh([quad(-10, -10, 10, 10)])
    text = f"""[gd_scene format=3]

[ext_resource type="Texture2D" path="res://textures/{name}.png" id="sign_texture"]
[ext_resource type="Material" path="res://materials/asphalt.tres" id="asphalt"]

[sub_resource type="BoxShape3D" id="road_shape"]
size = Vector3(20, 1, 20)

[sub_resource type="BoxMesh" id="road_mesh"]
material = ExtResource("asphalt")
size = Vector3(20, 1, 20)

[sub_resource type="BoxShape3D" id="building_shape"]
size = Vector3(8, 12, 20)

[sub_resource type="StandardMaterial3D" id="facade"]
albedo_color = Color({r:.3f}, {g:.3f}, {b:.3f}, 1)
roughness = 0.85

[sub_resource type="BoxMesh" id="building_mesh"]
material = SubResource("facade")
size = Vector3(8, 12, 20)

[sub_resource type="StandardMaterial3D" id="sign_material"]
albedo_texture = ExtResource("sign_texture")
emission_enabled = true
emission_texture = ExtResource("sign_texture")
emission_energy_multiplier = 2.0

[sub_resource type="QuadMesh" id="sign_mesh"]
material = SubResource("sign_material")
size = Vector2(6, 6)

[sub_resource type="CylinderMesh" id="pole_mesh"]
top_radius = 0.1
bottom_radius = 0.12
height = 6.0

{nav}
[node name="{name.title().replace("_", "")}" type="Node3D"]

[node name="Road" type="StaticBody3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, -0.5, 0)

[node name="Shape" type="CollisionShape3D" parent="Road"]
shape = SubResource("road_shape")

[node name="Mesh" type="MeshInstance3D" parent="Road"]
mesh = SubResource("road_mesh")

[node name="BuildingWest" type="StaticBody3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, -14, 6, 0)

[node name="Shape" type="CollisionShape3D" parent="BuildingWest"]
shape = SubResource("building_shape")

[node name="Mesh" type="MeshInstance3D" parent="BuildingWest"]
mesh = SubResource("building_mesh")
"""
    if not shop_side:
        text += """
[node name="BuildingEast" type="StaticBody3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 14, 6, 0)

[node name="Shape" type="CollisionShape3D" parent="BuildingEast"]
shape = SubResource("building_shape")

[node name="Mesh" type="MeshInstance3D" parent="BuildingEast"]
mesh = SubResource("building_mesh")
"""
    text += f"""
[node name="Sign" type="MeshInstance3D" parent="."]
transform = Transform3D(0, 0, 1, 0, 1, 0, -1, 0, 0, -9.9, 5, 0)
mesh = SubResource("sign_mesh")

[node name="Neon" type="OmniLight3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, -8, 5, 0)
light_color = Color({lr:.3f}, {lg:.3f}, {lb:.3f}, 1)
light_energy = 2.0
shadow_enabled = {"true" if index % 2 == 0 else "false"}
omni_range = 12.0

[node name="Pole" type="MeshInstance3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 9, 3, 5)
mesh = SubResource("pole_mesh")

[node name="Lamp" type="SpotLight3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 0, 1, 0, -1, 0, 8, 6, 5)
light_energy = 3.0
shadow_enabled = true
spot_range = 10.0
spot_angle = 50.0

[node name="Navigation" type="NavigationRegion3D" parent="."]
navigation_mesh = SubResource("nav")
"""
    write(f"cells/{name}.tscn", text)


def shop_scene():
    # Local space: the cell is centered at the shop's middle; the doorway is
    # in the west wall (x = -8), z = -1 .. 1.
    nav = navmesh([quad(-8, -8, 8, 8)])
    text = f"""[gd_scene format=3]

[ext_resource type="Texture2D" path="res://textures/shop_sign.png" id="sign_texture"]
[ext_resource type="Script" path="res://scripts/door.gd" id="door"]
[ext_resource type="Script" path="res://scripts/light_switch.gd" id="light_switch"]

[sub_resource type="StandardMaterial3D" id="interior"]
albedo_color = Color(0.75, 0.7, 0.6, 1)

[sub_resource type="BoxShape3D" id="floor_shape"]
size = Vector3(16, 0.2, 16)

[sub_resource type="BoxMesh" id="floor_mesh"]
material = SubResource("interior")
size = Vector3(16, 0.2, 16)

[sub_resource type="BoxShape3D" id="long_wall_shape"]
size = Vector3(16, 4, 0.2)

[sub_resource type="BoxMesh" id="long_wall_mesh"]
material = SubResource("interior")
size = Vector3(16, 4, 0.2)

[sub_resource type="BoxShape3D" id="end_wall_shape"]
size = Vector3(0.2, 4, 16)

[sub_resource type="BoxMesh" id="end_wall_mesh"]
material = SubResource("interior")
size = Vector3(0.2, 4, 16)

[sub_resource type="BoxShape3D" id="door_wall_shape"]
size = Vector3(0.2, 4, 7)

[sub_resource type="BoxMesh" id="door_wall_mesh"]
material = SubResource("interior")
size = Vector3(0.2, 4, 7)

[sub_resource type="BoxShape3D" id="door_shape"]
size = Vector3(0.1, 3, 2)

[sub_resource type="StandardMaterial3D" id="door_material"]
albedo_color = Color(0.35, 0.2, 0.1, 1)

[sub_resource type="BoxMesh" id="door_mesh"]
material = SubResource("door_material")
size = Vector3(0.1, 3, 2)

[sub_resource type="BoxShape3D" id="shelf_shape"]
size = Vector3(4, 2, 1)

[sub_resource type="BoxMesh" id="shelf_mesh"]
size = Vector3(4, 2, 1)

[sub_resource type="StandardMaterial3D" id="sign_material"]
albedo_texture = ExtResource("sign_texture")
emission_enabled = true
emission_texture = ExtResource("sign_texture")

[sub_resource type="QuadMesh" id="sign_mesh"]
material = SubResource("sign_material")
size = Vector2(3, 3)

{nav}
[node name="Shop" type="Node3D"]

[node name="Floor" type="StaticBody3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, -0.1, 0)

[node name="Shape" type="CollisionShape3D" parent="Floor"]
shape = SubResource("floor_shape")

[node name="Mesh" type="MeshInstance3D" parent="Floor"]
mesh = SubResource("floor_mesh")

[node name="Ceiling" type="StaticBody3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 4.1, 0)

[node name="Shape" type="CollisionShape3D" parent="Ceiling"]
shape = SubResource("floor_shape")

[node name="Mesh" type="MeshInstance3D" parent="Ceiling"]
mesh = SubResource("floor_mesh")
"""
    walls = [
        ("WallNorth", "long_wall", (0, 2, -8)),
        ("WallSouth", "long_wall", (0, 2, 8)),
        ("WallEast", "end_wall", (8, 2, 0)),
        ("WallDoorNorth", "door_wall", (-8, 2, -4.5)),
        ("WallDoorSouth", "door_wall", (-8, 2, 4.5)),
        ("ShelfA", "shelf", (2, 1, -5)),
        ("ShelfB", "shelf", (2, 1, 5)),
    ]
    for name, shape, (x, y, z) in walls:
        text += f"""
[node name="{name}" type="StaticBody3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, {x}, {y}, {z})

[node name="Shape" type="CollisionShape3D" parent="{name}"]
shape = SubResource("{shape}_shape")

[node name="Mesh" type="MeshInstance3D" parent="{name}"]
mesh = SubResource("{shape}_mesh")
"""
    text += """
[node name="Door" type="AnimatableBody3D" parent="." groups=["level_stream_persistent"]]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, -8, 0, -1)
script = ExtResource("door")

[node name="Shape" type="CollisionShape3D" parent="Door"]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1.5, 1)
shape = SubResource("door_shape")

[node name="Mesh" type="MeshInstance3D" parent="Door"]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1.5, 1)
mesh = SubResource("door_mesh")

[node name="Light" type="OmniLight3D" parent="." groups=["level_stream_persistent"]]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 3.5, 0)
light_color = Color(1, 0.85, 0.6, 1)
light_energy = 1.5
shadow_enabled = true
omni_range = 12.0
script = ExtResource("light_switch")

[node name="Sign" type="MeshInstance3D" parent="."]
transform = Transform3D(0, 0, -1, 0, 1, 0, 1, 0, 0, 7.85, 2, 0)
mesh = SubResource("sign_mesh")

[node name="Navigation" type="NavigationRegion3D" parent="."]
navigation_mesh = SubResource("nav")
"""
    write("cells/shop.tscn", text)


def plaza_scene():
    nav = navmesh([quad(-20, -20, 20, 20)])
    text = f"""[gd_scene format=3]

[ext_resource type="Texture2D" path="res://textures/plaza.png" id="sign_texture"]
[ext_resource type="Material" path="res://materials/asphalt.tres" id="asphalt"]

[sub_resource type="BoxShape3D" id="ground_shape"]
size = Vector3(40, 1, 40)

[sub_resource type="BoxMesh" id="ground_mesh"]
material = ExtResource("asphalt")
size = Vector3(40, 1, 40)

[sub_resource type="StandardMaterial3D" id="sign_material"]
albedo_texture = ExtResource("sign_texture")
emission_enabled = true
emission_texture = ExtResource("sign_texture")

[sub_resource type="CylinderMesh" id="fountain_mesh"]
material = SubResource("sign_material")
top_radius = 4.0
bottom_radius = 4.5
height = 1.0

[sub_resource type="CylinderShape3D" id="fountain_shape"]
radius = 4.5
height = 1.0

{nav}
[node name="Plaza" type="Node3D"]

[node name="Ground" type="StaticBody3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, -0.5, 0)

[node name="Shape" type="CollisionShape3D" parent="Ground"]
shape = SubResource("ground_shape")

[node name="Mesh" type="MeshInstance3D" parent="Ground"]
mesh = SubResource("ground_mesh")

[node name="Fountain" type="StaticBody3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0.5, -10)

[node name="Shape" type="CollisionShape3D" parent="Fountain"]
shape = SubResource("fountain_shape")

[node name="Mesh" type="MeshInstance3D" parent="Fountain"]
mesh = SubResource("fountain_mesh")

[node name="Light" type="OmniLight3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 4, -10)
light_color = Color(0.6, 0.8, 1, 1)
light_energy = 3.0
shadow_enabled = true
omni_range = 16.0

[node name="Navigation" type="NavigationRegion3D" parent="."]
navigation_mesh = SubResource("nav")
"""
    write("cells/plaza.tscn", text)


def cell_node(name, scene, position, size, neighbors, preload=None, unload=None):
    x, y, z = position
    text = f"""
[node name="{name.title().replace("_", "")}" type="LevelStreamCell3D" parent="Streamer"]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, {x}, {y}, {z})
cell_id = &"{name}"
scene_path = "res://cells/{scene}.tscn"
size = Vector3({size[0]}, {size[1]}, {size[2]})
"""
    if preload is not None:
        text += f"preload_distance = {preload}\n"
    if unload is not None:
        text += f"unload_distance = {unload}\n"
    if neighbors:
        text += "neighbors = PackedStringArray(" + ", ".join(f'"{n}"' for n in neighbors) + ")\n"
    return text


def main_scene():
    text = """[gd_scene format=3]

[ext_resource type="Script" path="res://test_driver.gd" id="driver"]

[sub_resource type="ProceduralSkyMaterial" id="sky_material"]

[sub_resource type="Sky" id="sky"]
sky_material = SubResource("sky_material")

[sub_resource type="Environment" id="environment"]
background_mode = 2
sky = SubResource("sky")
tonemap_mode = 2

[node name="Main" type="Node3D"]
script = ExtResource("driver")

[node name="Environment" type="WorldEnvironment" parent="."]
environment = SubResource("environment")

[node name="Sun" type="DirectionalLight3D" parent="."]
transform = Transform3D(0.866, -0.354, 0.354, 0, 0.707, 0.707, -0.5, -0.612, 0.612, 0, 20, 0)
light_energy = 0.6
shadow_enabled = true

[node name="Player" type="Node3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0)

[node name="Camera3D" type="Camera3D" parent="Player"]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0.7, 0)
current = true
far = 400.0

[node name="Streamer" type="LevelStreamer3D" parent="."]
target_path = NodePath("../Player")
"""
    for i in range(STREET_CELLS):
        name = f"street_{i:02d}"
        neighbors = []
        if i > 0:
            neighbors.append(f"street_{i - 1:02d}")
        if i < STREET_CELLS - 1:
            neighbors.append(f"street_{i + 1:02d}")
        if i == SHOP_STREET:
            neighbors.append("shop")
        text += cell_node(name, name, (0, 0, i * CELL_LENGTH), (20, 10, 20), neighbors)
    shop_z = SHOP_STREET * CELL_LENGTH
    text += cell_node(
        "shop", "shop", (18, 0, shop_z), (16, 8, 16), [f"street_{SHOP_STREET:02d}"], preload=8.0, unload=25.0
    )
    text += cell_node("plaza", "plaza", (0, 0, 400), (40, 10, 40), ["annex"])
    text += cell_node("annex", "annex", (30, 0, 400), (20, 10, 20), ["plaza"])
    text += cell_node("depot", "depot", (0, 0, 600), (20, 10, 20), [])
    write("main.tscn", text)


def main():
    os.makedirs(os.path.join(ROOT, "textures"), exist_ok=True)
    make_asphalt()
    for i in range(STREET_CELLS):
        name = f"street_{i:02d}"
        make_sign(i, name)
        street_scene(i, name, i == SHOP_STREET)
    for name, index in EXTRA.items():
        make_sign(index, name)
        street_scene(index, name, False)
    make_sign(20, "shop_sign")
    make_sign(21, "plaza")
    shop_scene()
    plaza_scene()
    main_scene()


main()
