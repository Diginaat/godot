import os

from SCons.Script import ARGUMENTS

_notified = False


def get_opts(platform):
    # Registered as SCons options, so they can also come from custom.py (or a
    # profile), not only from the command line or the environment variables.
    from SCons.Variables import BoolVariable

    return [
        ("physx_sdk", "PhysX 5 SDK install folder (printed by misc/build_physx.py)", os.environ.get("PHYSX_SDK", "")),
        BoolVariable("physx_gpu", "Build against the PhysX GPU SDK (CUDA GPU dynamics)", False),
        ("blast_sdk", "NVIDIA Blast SDK folder (printed by misc/build_physx.py --blast)", os.environ.get("BLAST_SDK", "")),
        ("flow_sdk", "NVIDIA Flow folder (printed by misc/build_physx.py --flow)", os.environ.get("FLOW_SDK", "")),
    ]


def _physx_sdk_path(env=None):
    if env is not None and env.get("physx_sdk"):
        return env["physx_sdk"]
    return ARGUMENTS.get("physx_sdk", os.environ.get("PHYSX_SDK", ""))


def can_build(env, platform):
    global _notified

    if env["disable_physics_3d"]:
        return False

    # The module links against an out-of-tree PhysX 5 SDK (it is not vendored).
    # With no SDK configured, quietly skip it so a stock build still succeeds.
    if not _physx_sdk_path(env):
        if not _notified:
            print(
                "godot_physx: no PhysX SDK configured, module disabled. "
                "Pass physx_sdk=<path> or set PHYSX_SDK, or run "
                "modules/godot_physx/misc/build_physx.py. See modules/godot_physx/README.md."
            )
            _notified = True
        return False

    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "PhysXParticleFluid3D",
        "PhysXCloth3D",
        "PhysXSkinnedCloth3D",
        "PhysXChunkEmitter3D",
        "PhysXDestructible3D",
        "PhysXBlastAsset",
        "PhysXFlow3D",
        "PhysXFlowEmitter3D",
        "PhysXFlowRenderEffect",
        "PhysXVehicle3D",
        "PhysXVehicleWheel3D",
        "PhysXMotorcycle3D",
        "PhysXTank3D",
        "PhysXWaterSurface3D",
        "PhysXBuoyancy3D",
        "PhysXBoat3D",
        "PhysXWaterWake3D",
        "PhysXWaterSpray3D",
        "PhysXGranular3D",
        "PhysXGas3D",
        "PhysXGasEmitter3D",
        "PhysXBlastAuthoring",
        "WaterRippleProbe",
        "GodotPhysXVehicleProbe",
        "GodotPhysXMotorcycleProbe",
        "GodotPhysXTankProbe",
        "GodotPhysXBlastProbe",
    ]


def get_doc_path():
    return "doc_classes"
