Import("env") # type: ignore

import time


def wait_after_upload(source, target, env):
    print("Waiting for USB serial port to reconnect...")
    time.sleep(1)


env.AddPostAction("upload", wait_after_upload) # type: ignore
