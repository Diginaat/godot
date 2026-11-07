def can_build(env, platform):
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "Weather",
        "WeatherEffect",
        "WeatherLibrary",
        "WeatherLightingEffect",
        "WeatherProfile",
        "WindReceiver3D",
        "WindServer",
    ]


def get_doc_path():
    return "doc_classes"
