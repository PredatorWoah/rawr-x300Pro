import json, subprocess, sys
snapshot = json.loads(subprocess.check_output([sys.argv[1], '--snapshot']))
assert snapshot['whiteBalanceRequestId'] == 42
assert snapshot['whiteBalanceMode'] == 9
assert snapshot['hasAutoWbEstimate'] is True
assert snapshot['autoWbEstimateCalibrated'] is False
assert snapshot['autoWbTemperatureK'] == snapshot['whiteBalanceTemperatureK'] == 6600
assert snapshot['autoWbTint'] == snapshot['whiteBalanceTint'] == 0
print('CAMERA_WHITE_BALANCE_SNAPSHOT_PASS')
