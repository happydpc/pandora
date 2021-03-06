from parsing.parser import Texture
from unique_collection import UniqueCollection
import numpy as np

def option_picker(options, option_name):
    if len(options) > 1:
        print(f"File contains multiple {option_name}s, select one:")
        for i, option in enumerate(options):
            print("{}: {}".format(i, option))
        
        while True:
            i = int(input(f"Enter the index of the desired {option_name}: "))
            if i >= 0 and i < len(options):
                return options[i]
            else:
                print("Out of range; try again.")
    else:
        return options[0]


class ConfigParser:
    def __init__(self, pbrt_config):
        self._camera = self._get_camera(pbrt_config)

    def _get_camera(self, pbrt_config):
        film = option_picker(pbrt_config["films"], "film")

        resolution = (film["arguments"]["xresolution"]["value"],
                      film["arguments"]["yresolution"]["value"])
        
        camera = pbrt_config["camera"]
        assert(camera.type == "perspective")
        camera_type = camera.type
        fov = camera.arguments["fov"]["value"]
        camera_to_world_transform = np.linalg.inv(np.array(camera.world_to_camera_transform).reshape(4,4)).tolist()

        return {
            "type": camera_type,
            "resolution": resolution,
            "fov": fov,
            "camera_to_world_transform": camera_to_world_transform
        }

    def data(self):
        return {
            "camera": self._camera
        }
