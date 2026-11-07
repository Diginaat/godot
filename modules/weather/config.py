def can_build(env, platform):
    # The built-in sky's cloud textures are NoiseTexture2D.
    env.module_add_dependencies("weather", ["noise"])
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "Weather",
        "WeatherEffect",
        "WeatherHazeEffect",
        "WeatherLibrary",
        "WeatherLightingEffect",
        "WeatherLightningEffect",
        "WeatherMotesEffect",
        "WeatherProfile",
        "WeatherRainEffect",
        "WeatherSkyEffect",
        "WeatherSoundEffect",
        "WindReceiver3D",
        "WindServer",
    ]


def get_doc_path():
    return "doc_classes"
