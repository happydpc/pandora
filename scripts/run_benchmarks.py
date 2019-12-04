import subprocess
import os
import re
import shared_benchmark_code


result_output_folder = "C:/Users/Mathijs/OneDrive/TU Delft/Batched Ray Traversal/Results/"

def run_pandora_with_defaults(scene, out_folder, geom_cache, bvh_cache, svdag_res):
	args = [
		"--file", scene["file"],
		"--subdiv", scene["subdiv"],
		"--cameraid", 0,
		"--integrator", "path",
		"--spp", 4,
		"--concurrency", 16000000,
		"--schedulers", 4,
		"--geomcache", geom_cache,
		"--bvhcache", bvh_cache,
		"--primgroup", scene["batch_point_size"],
		"--svdagres", svdag_res
	]
	shared_benchmark_code.run_pandora(args, out_folder)


def test_svdag_no_mem_limit(scenes, num_runs = 1):
	for svdag_res in [32, 64, 128, 256, 512]:
		for scene in scenes:
			for run in range(num_runs):
				print(f"Benchmarking {scene['name']} at SVDAG res {svdag_res} (run {run})")
				out_folder = os.path.join(result_output_folder, "svdag_res", svdag_res, scene["name"], f"run-{run}")
				run_pandora_with_defaults(
					scene,
					out_folder,
					1000*1000, # Unlimited geom cache
					1000*1000, # Unlimited BVH cache
					svdag_res)


# Test as memory limit (1.0 = full memory, 0.5 = 50%, etc...)
def test_at_memory_limit(scenes, result_folder, geom_memory_limit, bvh_memory_limit, culling, num_runs = 1):
	for scene in scenes:
		for run in range(num_runs):
			mem_limit_percentage = int(geom_memory_limit * 100)
			culling_str = "culling" if culling else "no-culling"
			print(f"Benchmarking {scene['name']} at memory limit {mem_limit_percentage} with {culling_str} (run {run})")
			out_folder = os.path.join(result_output_folder, "mem_limit", str(mem_limit_percentage), culling_str, scene["name"], f"run-{run}")
			run_pandora_with_defaults(
				scene,
				out_folder,
				int(scene["max_geom_mem_mb"] * geom_memory_limit),
				int(scene["max_bvh_mem_mb"] * bvh_memory_limit),
				32)

if __name__ == "__main__":
	scenes = shared_benchmark_code.get_scenes()
	#test_svdag_no_mem_limit(scenes, 1)
	test_at_memory_limit(scenes, 0.9, 0.5, False, 1)