Import("env")
import shutil
import os

def copy_firmware(source, target, env):
    target_firmpath = str(target[0])
    project_dir = env.get("PROJECT_DIR")
    dest_firmpath = os.path.join(project_dir, "firmware.bin")
    
    print(f"Copying firmware to root: {dest_firmpath}")
    shutil.copy(target_firmpath, dest_firmpath)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_firmware)
