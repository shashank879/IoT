import os
import sys
current_dir = os.path.dirname(__file__)
common_pylib = os.path.join(current_dir, 'common', 'python')
sys.path.append(common_pylib)

import json
import time
from servers.AudioServices.audio_vis import AudioVisualizerService


dir_path = os.path.dirname(os.path.realpath(__file__))
with open(os.path.join(dir_path, 'passwd.json')) as f:
    user_pass_info = json.load(f)


try:
    audio_vis_service_name = 'service/audio_vis'
    audio_vis_service = AudioVisualizerService(
        audio_vis_service_name,
        user_pass_info[audio_vis_service_name],
        autostart=True,
        keepalive=12000)
    audio_vis_service(fps=30)
except Exception as e:
    raise e
