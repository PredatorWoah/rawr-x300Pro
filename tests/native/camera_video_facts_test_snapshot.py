import json, subprocess, sys
snapshot = json.loads(subprocess.check_output([sys.argv[1], '--snapshot']))
camera, missing = snapshot['cameras']
assert camera['id'] == 'private"camera\\\n'
assert camera['sensorOrientationDegrees'] == 90
assert camera['lensFacing'] == 1 and camera['timestampSource'] == 1
assert camera['raw'][0]['minFrameDurationNs'] == 0
assert camera['raw'][1]['minFrameDurationNs'] == 33333333
assert camera['raw'][2]['minFrameDurationNs'] is None
assert missing['sensorOrientationDegrees'] is None
assert missing['lensFacing'] is None and missing['timestampSource'] is None
assert missing['raw'] == [] and missing['error'] == 'Unavailable\nmetadata'
assert snapshot['encoderProbeModes'] == [
    {'width': 1920, 'height': 1080}, {'width': 3840, 'height': 2160}, {'width': 4080, 'height': 3064}
]
assert snapshot['discoveryError'] == 'Discovery\tpartial'
print('CAMERA_VIDEO_SNAPSHOT_CONTRACT_PASS')
