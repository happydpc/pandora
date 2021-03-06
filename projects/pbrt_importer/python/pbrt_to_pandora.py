import argparse
import os
import pickle
import parsing
from config_parser import ConfigParser
from scene_parser import SceneParser
import json
import ujson  # Faster than regular json module
from unique_collection import UniqueCollection
import numpy as np
import itertools
import collections.abc
import shutil
from sqlitedict import SqliteDict


def extract_pandora_data(pbrt_data, out_mesh_folder):
    if not os.path.exists(out_mesh_folder):
        os.makedirs(out_mesh_folder)

    config_data = ConfigParser(pbrt_data["config"])
    scene_data = SceneParser(pbrt_data["scene"], out_mesh_folder)
    return {
        "config": config_data.data(),
        "scene": scene_data.data()
    }


class MyJSONEncoder(json.JSONEncoder):
    # Iterable encoder to save memory
    class FakeListIterator(list):
        def __init__(self, iterable):
            self.iterable = iter(iterable)
            try:
                self.firstitem = next(self.iterable)
                self.truthy = True
            except StopIteration:
                self.truthy = False

        def __iter__(self):
            if not self.truthy:
                return iter([])
            return itertools.chain([self.firstitem], self.iterable)

        def __len__(self):
            raise NotImplementedError("Fakelist has no length")

        def __getitem__(self, i):
            raise NotImplementedError("Fakelist has no getitem")

        def __setitem__(self, i):
            raise NotImplementedError("Fakelist has no setitem")

        def __bool__(self):
            return self.truthy

    def default(self, obj):
        if isinstance(obj, UniqueCollection):
            return type(self).FakeListIterator(obj)
        elif isinstance(obj, np.ndarray):
            return obj.tolist()
        return json.JSONEncoder.default(self, obj)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Export preprocessed PBRT scenes to Pandora's JSON format")
    parser.add_argument("--file", type=str, required=True,
                        help="Either the PBRT scene file OR the intermediate file that was outputted on a previous run")
    parser.add_argument("--out", type=str, required=True,
                        help="Path/name of output Pandora scene description file")
    parser.add_argument("--intermediate", default=False, action="store_true",
                        help="Whether to output intermediate files")
    args = parser.parse_args()

    if not os.path.isfile(args.file):
        print("Error: pbrt scene file not found")
        exit(1)
    if not os.path.exists(os.path.dirname(args.out)):
        os.makedirs(os.path.dirname(args.out))

    # Replaces forward slashes by backward slashes on Windows
    args.file = os.path.normpath(args.file)
    args.out = os.path.normpath(args.out)

    out_folder = os.path.dirname(args.out)
    int_mesh_folder = os.path.join(os.path.dirname(args.out), "pbrt_meshes")

    if args.file.endswith(".pbrt"):
        print("==== PARSING PBRT FILE ====")
        pbrt_data = parsing.parse_file(args.file, int_mesh_folder)
    elif args.file.endswith(".bin"):
        with open(args.file, "rb") as f:
            pbrt_data = pickle.load(f)
            pbrt_data["scene"]["instance_templates"] = SqliteDict(pbrt_data["scene"]["instance_templates"])
    else:
        print("Input file has unknown file extension")
        exit(-1)

    if args.intermediate and not args.file.endswith(".bin"):
        print("==== STORING INTERMEDIATE DATA TO A FILE ====")
        int_file = os.path.join(os.path.dirname(args.out), "intermediate.bin")
        with open(int_file, "wb") as f:
            # Database cannot be pickled so store name of database file
            textures_db = pbrt_data["scene"]["textures"]
            instance_templates_db = pbrt_data["scene"]["instance_templates"]
            pbrt_data["scene"]["textures"] = textures_db.filename
            pbrt_data["scene"]["instance_templates"] = instance_templates_db.filename

            pickle.dump(pbrt_data, f)

            pbrt_data["scene"]["textures"] = textures_db
            pbrt_data["scene"]["instance_templates"] = instance_templates_db

    print("==== CONVERTING TO PANDORA FORMAT ====")
    pandora_data = extract_pandora_data(pbrt_data, out_folder)

    if args.intermediate:
        print("==== STORING PICKLE BACKUP ====")
        with open(os.path.join(out_folder, "BACKUP.bin"), "wb") as f:
            pickle.dump(pandora_data, f)

    print("==== WRITING PANDORA SCENE FILE ====")
    with open(args.out, "w") as f:
        print(f"Out file: {args.out}")
        #json.dump(pandora_data, f, cls=MyJSONEncoder)
        ujson.dump(pandora_data, f, indent=2)
        #ujson.dump(pandora_data, f,)# No formatting saves a lot of disk space


    """if not args.intermediate:
        shutil.rmtree(os.path.join(out_folder, "pbrt_meshes"))

        # Remove the temporary list files created for the json export
        shutil.rmtree(os.path.join(out_folder, "tmp_lists"))"""

